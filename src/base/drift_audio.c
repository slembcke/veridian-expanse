#include <stdbool.h>
#include <string.h>
#include <stdatomic.h>

#include <SDL.h>
#include <tracy/TracyC.h>

#undef alloca
#define STB_VORBIS_NO_PUSHDATA_API
#define STB_VORBIS_NO_INTEGER_CONVERSION
#include "stb/stb_vorbis.c"

#include "drift_base.h"

#define MAX_AUDIO_SOURCES 64
#define MAX_AUDIO_SOURCE_DATA_SIZE 64

// https://www.gcaudio.com/tips-tricks/the-relationship-of-voltage-loudness-power-and-decibels/

static float dB_to_power(float db){return powf(10, db/10);}
static float dB_to_gain(float db){return powf(10, db/20);}

static float vol_to_gain(float vol){
	// 1.661 = log(10)/log(4) -> 10x voltage means 4x loudness.
	return powf(vol, 1.661f);
}

typedef bool audio_source_func(void* data, uint source_idx, float* stereo_frames, size_t frame_count);

typedef struct {
	audio_source_func* func;
	u8 bytes[MAX_AUDIO_SOURCE_DATA_SIZE];
} AudioSourceData;

typedef struct {
	mtx_t mutex;
	
	struct {
		uint head, tail, cursor;
		u16 arr[MAX_AUDIO_SOURCES];
	} pool;
	
	struct {
		uint head, tail;
		DriftAudioSourceID arr[MAX_AUDIO_SOURCES];
	} queue;
	
	u16 generation[MAX_AUDIO_SOURCES];
	AudioSourceData data[MAX_AUDIO_SOURCES];
	DriftAudioSourceID arr[MAX_AUDIO_SOURCES];
	uint count;
} AudioSources;

static uint audio_source_active(AudioSources* sources, DriftAudioSourceID source){
	uint idx = source.id % MAX_AUDIO_SOURCES;
	return sources->generation[idx] == source.id >> 16 ? idx : 0;
}

static DriftAudioSourceID audio_source_aquire(AudioSources* sources){
	uint pool_count = (sources->pool.head - sources->pool.tail) % MAX_AUDIO_SOURCES;
	if(pool_count > 1){ // TODO
		atomic_thread_fence(memory_order_acquire);
		uint idx = sources->pool.arr[sources->pool.tail++ % MAX_AUDIO_SOURCES];
		return (DriftAudioSourceID){idx | sources->generation[idx] << 16};
	} else if(sources->pool.cursor < MAX_AUDIO_SOURCES){
		// Allocate a new index.
		return (DriftAudioSourceID){sources->pool.cursor++};
	} else {
		// No available sources!
		DRIFT_LOG("Audio source pool exhausted!");
		return (DriftAudioSourceID){0};
	}
}

static void audio_source_retire(AudioSources* sources, DriftAudioSourceID source){
	uint source_idx = source.id % MAX_AUDIO_SOURCES;
	DRIFT_ASSERT_WARN(source_idx, "Trying to remove an audio source that does not exist. (%d)", source.id);
	// DRIFT_LOG("retired: a%08X", source.id);
	
	sources->generation[source_idx]++;
	sources->pool.arr[sources->pool.head % MAX_AUDIO_SOURCES] = source_idx;
	atomic_thread_fence(memory_order_release);
	sources->pool.head++;
}

static void decode_music_loop(stb_vorbis* music, float interleaved_samples[], size_t frame_count){
	size_t frames_decoded = stb_vorbis_get_samples_float_interleaved(music, 2, interleaved_samples, 2*frame_count);
	
	if(frames_decoded < frame_count){
		stb_vorbis_seek_start(music);
		decode_music_loop(music, interleaved_samples + 2*frames_decoded, frame_count - frames_decoded);
	}
}

typedef struct {
	float* samples;
	uint length;
} DriftAudioSample;

static void mix_samples(float dst[], float src[], size_t count, float volume){
	float* end = dst + count;
	while(dst < end) (*dst++) += (*src++)*volume;
}

struct DriftAudioContext {
	SDL_AudioDeviceID id;
	SDL_AudioSpec spec;
	float master_gain;
	
	float music_gain;
	stb_vorbis* music;
	
	DriftAudioSample sample_bank[_DRIFT_SFX_COUNT];
	AudioSources sources;
};

static void audio_callback(DriftAudioContext* ctx, void* stream, int stream_len){
	memset(stream, 0, stream_len);
	uint frame_count = ctx->spec.samples;
	float* interleaved_samples = stream;
	
	float music_buffer[2*frame_count];
	if(ctx->music){
		decode_music_loop(ctx->music, music_buffer, frame_count);
		mix_samples(interleaved_samples, music_buffer, 2*frame_count, ctx->music_gain);
	}
	
	AudioSources* sources = &ctx->sources;
	
	uint queue_head = sources->queue.head;
	atomic_thread_fence(memory_order_acquire);
	while(sources->queue.tail != queue_head){
		// Deque a pending source and push it onto the list.
		sources->arr[sources->count++] = sources->queue.arr[sources->queue.tail++ % MAX_AUDIO_SOURCES];
	}
	
	uint i = 0;
	while(i < sources->count){
		DriftAudioSourceID source = sources->arr[i];
		uint source_idx = audio_source_active(sources, source);
		DRIFT_ASSERT(source_idx, "Inactive audio source in list? a%08X, i:%d, count:%d", source.id, i, sources->count);
		
		AudioSourceData* data = sources->data + source_idx;
		if(source_idx && data->func && data->func(data->bytes, source_idx, interleaved_samples, frame_count)){
			audio_source_retire(sources, sources->arr[i]);
			// DRIFT_LOG("replacing arr[%d](a%08X) with sources->arr[%d](a%08X)", i, sources->arr[i], sources->count - 1, sources->arr[sources->count - 1]);
			sources->arr[i] = sources->arr[--sources->count];
			// DRIFT_LOG("i: %d, count: %d", i, sources->count);
		} else {
			i++;
		}
	}
	
	for(uint i = 0; i < 2*frame_count; i++){
		interleaved_samples[i] = DriftClamp(interleaved_samples[i], -1, +1)*ctx->master_gain;
	}
}

DriftAudioContext* DriftAudioContextNew(void){
	DriftAudioContext* ctx = DriftAlloc(DriftSystemMem, sizeof(*ctx));
	mtx_init(&ctx->sources.mutex, mtx_plain);
	ctx->sources.pool.cursor = 1;
	
	DRIFT_ASSERT(ctx->id == 0, "Audio device already open.");
	ctx->id = SDL_OpenAudioDevice(NULL, 0, &(SDL_AudioSpec){
		.freq = 44100, .format = AUDIO_F32SYS, .channels = 2, .samples = 1024,
		.callback = (SDL_AudioCallback)audio_callback, .userdata = ctx,
	}, &ctx->spec, 0);
	DRIFT_ASSERT_WARN(ctx->id, "Failed to initialize audio: %s", SDL_GetError());
	
	return ctx;
}

// void DriftAudioContextFree(DriftAudioContext* ctx){
// 	SDL_CloseAudioDevice(ctx->id);
// 	DriftDealloc(DriftSystemMem, ctx, sizeof(*ctx));
// }

void DriftAudioStartMusic(DriftAudioContext* ctx){
	// TODO leak.
	DriftData music_data = DriftAssetLoad(DriftSystemMem, "music/AutomatedBalance.ogg");
	
	// SDL_LockAudioDevice(ctx->id);
	size_t sample_seek = 0;
	if(ctx->music){
		sample_seek = stb_vorbis_get_sample_offset(ctx->music);
		stb_vorbis_close(ctx->music);
	}

	int err = 0;
	ctx->music = stb_vorbis_open_memory(music_data.ptr, music_data.size, &err, NULL);
	DRIFT_ASSERT(ctx->music, "stb_vorbis failed to open music. %d", err);
	
	stb_vorbis_seek(ctx->music, sample_seek);
	// SDL_UnlockAudioDevice(ctx->id);
}

void DriftAudioPause(DriftAudioContext* ctx, bool state){
	SDL_PauseAudioDevice(ctx->id, state);
}

static float db_to_gain(float db){
	return DriftClamp(powf(10, db/10), 0, 1);
}

void DriftAudioSetParams(DriftAudioContext* ctx, float master_volume, float music_volume){
	ctx->master_gain = vol_to_gain(master_volume);
	ctx->music_gain = vol_to_gain(music_volume);
}

// TODO this leaks samples when hot loading I guess?
static void load_sample(tina_job* job){
	TracyCZoneN(ZONE_LOAD, "Load Sample", true);
	DriftAudioContext* ctx = tina_job_get_description(job)->user_data;
	uint idx = tina_job_get_description(job)->user_idx;
	
	static const char* NAMES[_DRIFT_SFX_COUNT] = {
		[DRIFT_SFX_ENGINE] = "sfx/engine.ogg",
		[DRIFT_SFX_PING] = "sfx/ping.ogg",
		[DRIFT_SFX_HORNS] = "sfx/ominous_horns.ogg",
		[DRIFT_SFX_TEXT_BLIP] = "sfx/text_blip.ogg",
		[DRIFT_SFX_CLICK] = "sfx/click.ogg",
		[DRIFT_SFX_BULLET_FIRE] = "sfx/bullet_fire.ogg",
		[DRIFT_SFX_BULLET_HIT] = "sfx/bullet_hit.ogg",
		[DRIFT_SFX_EXPLODE] = "sfx/explode.ogg",
	};
	
	DriftData data = DriftAssetLoad(DriftSystemMem, NAMES[idx]);
	DRIFT_ASSERT(data.ptr, "no sample");
	
	int err = 0;
	stb_vorbis* v = stb_vorbis_open_memory(data.ptr, data.size, &err, NULL);
	DRIFT_ASSERT(v, "bad samples");
	DRIFT_ASSERT(v->channels == 1, "bad channels");
	DRIFT_ASSERT(v->sample_rate == 44100, "bad rate");
	
	uint sample_count = stb_vorbis_stream_length_in_samples(v);
	float* samples = DriftAlloc(DriftSystemMem, sample_count*sizeof(*samples));
	uint decoded = stb_vorbis_get_samples_float_interleaved(v, 1, samples, sample_count);
	DRIFT_ASSERT(sample_count == decoded, "wrong length");
	
	ctx->sample_bank[idx] = (DriftAudioSample){.samples = samples, .length = sample_count};
	TracyCZoneEnd(ZONE_LOAD);
}

void DriftAudioLoadSamples(DriftAudioContext* ctx, tina_job* job){
	tina_job_description desc = {
		.name = "JobLoadSample", .func = load_sample, .queue_idx = DRIFT_JOB_QUEUE_WORK,
		.user_data = ctx, .user_idx = 0
	};
	
	tina_job_description jobs[_DRIFT_SFX_COUNT] = {};
	while(desc.user_idx < _DRIFT_SFX_COUNT) jobs[desc.user_idx] = desc, desc.user_idx++;
	
	tina_group group = {};
	tina_scheduler_enqueue_batch(tina_job_get_scheduler(job), jobs, _DRIFT_SFX_COUNT, &group, 0);
	
	tina_job_wait(job, &group, 0);
}

#define SAMPLER_FRACT_BITS 32

typedef struct {
	float* samples;
	u64 cursor, end;
	float gain, pan, pitch, gain0;
	bool loop;
} SamplerData;

_Static_assert(sizeof(SamplerData) < MAX_AUDIO_SOURCE_DATA_SIZE, "SamplerData is too big.");

static bool decode_sampler(void* data, uint source_idx, float* stereo_frames, size_t frame_count){
	SamplerData *sampler = data;
	
	u64 cursor_inc = (u64)(sampler->pitch*(1ull << SAMPLER_FRACT_BITS));
	float gain_inc = (sampler->gain - sampler->gain0)/frame_count;
	for(uint i = 0; i < frame_count; i++){
		if(sampler->cursor >= sampler->end){
			if(sampler->loop) sampler->cursor -= sampler->end; else return true;
		}
		
		float s = sampler->gain0*sampler->samples[sampler->cursor >> SAMPLER_FRACT_BITS];
		sampler->cursor += cursor_inc;
		sampler->gain0 += gain_inc;
		
		(*stereo_frames++) += s*DriftClamp(1 - sampler->pan, 0, 1);
		(*stereo_frames++) += s*DriftClamp(1 + sampler->pan, 0, 1);
	}
	
	return false;
}

DriftAudioSampler DriftAudioPlaySample(DriftAudioContext* ctx, DriftSFX sfx, float gain, float pan, float pitch, bool loop){
	if(ctx->id == 0) return (DriftAudioSampler){};
	
	DriftAudioSample* sample = ctx->sample_bank + sfx;
	SamplerData data = {
		.samples = sample->samples, .gain = gain, .pan = pan, .pitch = pitch, .loop = loop,
		.cursor = 0, .end = (u64)sample->length << SAMPLER_FRACT_BITS,
	};
	
	mtx_lock(&ctx->sources.mutex);
	DriftAudioSourceID source = audio_source_aquire(&ctx->sources);
	if(source.id){
		// DRIFT_LOG("source: 0x%X", source.id);
		
		uint source_idx = source.id % MAX_AUDIO_SOURCES;
		AudioSourceData* source_data = ctx->sources.data + source_idx;
		source_data->func = decode_sampler;
		memcpy(source_data->bytes, &data, sizeof(data));
		
		while(true){
			uint remain = (ctx->sources.queue.tail - ctx->sources.queue.head) % MAX_AUDIO_SOURCES;
			if(remain == 1){
				DRIFT_LOG("Audio queue is full!");
				continue;
			}
			
			ctx->sources.queue.arr[ctx->sources.queue.head % MAX_AUDIO_SOURCES] = source;
			atomic_thread_fence(memory_order_release);
			ctx->sources.queue.head++;
			break;
		}
	}
	mtx_unlock(&ctx->sources.mutex);
	
	return (DriftAudioSampler){source};
}


void DriftSamplerSetParams(DriftAudioContext* ctx, DriftAudioSampler sampler, float gain, float pan, float pitch, bool loop){
	// mtx_lock(&ctx->sources.mutex);
	uint source_idx = audio_source_active(&ctx->sources, sampler.source);
	if(source_idx){
		SamplerData* data = (SamplerData*)ctx->sources.data[source_idx].bytes;
		data->gain = gain;
		data->pan = pan;
		data->pitch = pitch;
		data->loop = loop;
	}
	// mtx_unlock(&ctx->sources.mutex);
}


bool DriftAudioSourceActive(DriftAudioContext* ctx, DriftAudioSourceID source){
	return audio_source_active(&ctx->sources, source) > 0;
}
