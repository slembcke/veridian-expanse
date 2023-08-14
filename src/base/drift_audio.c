/*
This file is part of Veridian Expanse.

Veridian Expanse is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

Veridian Expanse is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with Veridian Expanse. If not, see <https://www.gnu.org/licenses/>.
*/

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

typedef bool audio_source_func(float* stereo_frames, size_t frame_count, void* data);

typedef struct {
	uint bus_id;
	audio_source_func* func;
	u8 bytes[MAX_AUDIO_SOURCE_DATA_SIZE];
} AudioSourceData;

typedef struct {
	SDL_mutex* mutex;
	
	// Unused audio source ids
	struct {
		uint head, tail, cursor;
		u16 arr[MAX_AUDIO_SOURCES];
	} pool;
	
	// sources that are queue to start playing in the next render callback
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

typedef struct {
	DriftAudioSampler sampler;
	DriftAudioParams params;
} DriftImAudioSampler;

#define FILTER_MAXLEN 2048

typedef struct {
	float y0, y[FILTER_MAXLEN];
	size_t cursor, len;
} ReverbFilter;

static float comb_process(ReverbFilter* f, float x0, float feedback, float damp){
	float y0 = f->y[++f->cursor & (FILTER_MAXLEN - 1)];
	f->y0 = y0*(1 - damp) + f->y0*damp;
	f->y[(f->cursor + f->len) & (FILTER_MAXLEN - 1)] = x0 + f->y0*feedback;
	return y0;
}

static float allpass_process(ReverbFilter* f, float x0, float feedback){
	float y0 = f->y[++f->cursor & (FILTER_MAXLEN - 1)];
	f->y[(f->cursor + f->len) & (FILTER_MAXLEN - 1)] = x0 + (y0*feedback);
	return y0 - x0;
}

#define	NUM_COMBS 8
#define NUM_ALLPASSES 4
#define SCALE_ROOM 0.28f
#define OFFSET_ROOM 0.7f
#define FIXED_GAIN 0.015f
#define	STEREO_SPREAD 23

// These values assume 44.1KHz sample rate
// they will probably be OK for 48KHz sample rate
// but would need scaling for 96KHz (or other) sample rates.
// The values were obtained by listening tests.
static const size_t comb_tuning[] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
static const size_t allpass_tuning[] = {556, 441, 341, 225};

typedef struct {
	ReverbFilter combL[NUM_COMBS], allpassL[NUM_ALLPASSES];
	ReverbFilter combR[NUM_COMBS], allpassR[NUM_ALLPASSES];
	
	float	dry, wet, damp;
	float	roomsize, stereo_width;
} Reverb;

static void reverb_init(Reverb* reverb){
	memset(reverb, 0, sizeof(*reverb));
	reverb->dry = 1;
	reverb->wet = 0;
	reverb->damp = 0.5f;
	reverb->roomsize = 0.5f;
	reverb->stereo_width = 1;
	
	for(uint i = 0; i < NUM_COMBS; i++){
		reverb->combL[i] = (ReverbFilter){.len = comb_tuning[i]};
		reverb->combR[i] = (ReverbFilter){.len = comb_tuning[i] + STEREO_SPREAD};
	}
	
	for(uint i = 0; i < NUM_ALLPASSES; i++){
		reverb->allpassL[i] = (ReverbFilter){.len = allpass_tuning[i]};
		reverb->allpassR[i] = (ReverbFilter){.len = allpass_tuning[i] + STEREO_SPREAD};
	}
}

static void ReverbProcess(Reverb* Reverb, float* src, float* dst, size_t len){
	float wet_l = Reverb->wet*(1 + Reverb->stereo_width);
	float wet_r = Reverb->wet*(1 - Reverb->stereo_width);
	
	while(len-- > 0) {
		float sample = src[0] + src[1], l = 0, r = 0;
		
		// Accumulate comb filters in parallel
		for(int i = 0; i < NUM_COMBS; i++){
			l += comb_process(Reverb->combL + i, sample, Reverb->roomsize, Reverb->damp);
			r += comb_process(Reverb->combR + i, sample, Reverb->roomsize, Reverb->damp);
		}

		// Feed through allpasses in series
		for(int i = 0; i < NUM_ALLPASSES; i++){
			l = allpass_process(Reverb->allpassL + i, l, 0.5f);
			r = allpass_process(Reverb->allpassR + i, r, 0.5f);
		}

		dst[0] = src[0]*Reverb->dry + l*wet_l + r*wet_r;
		dst[1] = src[1]*Reverb->dry + r*wet_l + l*wet_r;
		src += 2, dst += 2;
	}
}

#define BLOCK_LEN 512

struct DriftAudioContext {
	SDL_AudioDeviceID id;
	SDL_AudioSpec spec;
	float master_gain;
	float music_gain;
	float effects_gain;
	
	stb_vorbis* music;
	
	bool bus_active[_DRIFT_BUS_COUNT];
	DRIFT_ARRAY(DriftAudioSample) sample_bank;
	AudioSources sources;
	
	DRIFT_ARRAY(DriftImAudioSampler) im_samplers;
	
	Reverb reverb;
};

// static void convolve_long(tina_job* job){
// 	DriftAudioContext* ctx = tina_job_get_description(job)->user_data;
// 	uint idx = tina_job_get_description(job)->user_idx;
// 	float* buffer = ctx->long_buffer;// + idx*IR_LONG_LEN*BLOCK_LEN;
// 	ir_convolve(ctx->ir_long, buffer, buffer);
// }

static void audio_callback(DriftAudioContext* ctx, void* stream, int stream_len){
	TracyCZoneN(AUDIO_ZONE, "Audio", true);
	memset(stream, 0, stream_len);
	uint frame_count = ctx->spec.samples;
	float* interleaved_samples = stream;
	
	TracyCZoneN(MUSIC_ZONE, "Music", true);
	float music_buffer[2*frame_count];
	if(ctx->music) decode_music_loop(ctx->music, music_buffer, frame_count);
	TracyCZoneEnd(MUSIC_ZONE);
	
	TracyCZoneN(SOURCES_ZONE, "Sources", true);
	AudioSources* sources = &ctx->sources;
	uint queue_head = sources->queue.head;
	atomic_thread_fence(memory_order_acquire);
	while(sources->queue.tail != queue_head){
		// Deque a pending source and push it onto the list.
		sources->arr[sources->count++] = sources->queue.arr[sources->queue.tail++ % MAX_AUDIO_SOURCES];
	}
	
	float busses[_DRIFT_BUS_COUNT][2*frame_count];
	memset(busses, 0, _DRIFT_BUS_COUNT*stream_len);
	
	for(uint i = sources->count - 1; i < sources->count; i--){
		DriftAudioSourceID source = sources->arr[i];
		uint source_idx = audio_source_active(sources, source);
		DRIFT_ASSERT(source_idx, "Inactive audio source in list? a%08X, i:%d, count:%d", source.id, i, sources->count);
		
		AudioSourceData* data = sources->data + source_idx;
		if(source_idx && ctx->bus_active[data->bus_id]){
			if(!data->func || data->func(busses[data->bus_id], frame_count, data->bytes)){
				audio_source_retire(sources, sources->arr[i]);
				sources->arr[i] = sources->arr[--sources->count];
			}
		}
	}
	TracyCZoneEnd(SOURCES_ZONE);
	
	if(ctx->bus_active[DRIFT_BUS_SFX]){
		float* samples = busses[DRIFT_BUS_SFX];
		
		ReverbProcess(&ctx->reverb, samples, samples, BLOCK_LEN);
	}
	
	TracyCZoneN(MIX_ZONE, "Bus mix", true);
	for(uint i = 0; i < 2*frame_count; i++){
		float sample = music_buffer[i]*ctx->music_gain;
		for(uint bus = 0; bus < _DRIFT_BUS_COUNT; bus++) sample += busses[bus][i]*ctx->effects_gain;
		interleaved_samples[i] = DriftClamp(sample, -1, +1)*ctx->master_gain;
	}
	TracyCZoneEnd(MIX_ZONE);
	TracyCZoneEnd(AUDIO_ZONE);
}

DriftAudioContext* DriftAudioContextNew(tina_scheduler* sched){
	DriftAudioContext* ctx = DriftAlloc(DriftSystemMem, sizeof(*ctx));
	ctx->sources.mutex = SDL_CreateMutex();
	ctx->sources.pool.cursor = 1;
	ctx->im_samplers = DRIFT_ARRAY_NEW(DriftSystemMem, 0, DriftImAudioSampler);
	
	reverb_init(&ctx->reverb);
	
	// Make all busses active.
	for(uint i = 0; i < _DRIFT_BUS_COUNT; i++) ctx->bus_active[i] = true;
	
	DRIFT_ASSERT(ctx->id == 0, "Audio device already open.");
	ctx->id = SDL_OpenAudioDevice(NULL, 0, &(SDL_AudioSpec){
		.freq = 44100, .format = AUDIO_F32SYS, .channels = 2, .samples = BLOCK_LEN,
		.callback = (SDL_AudioCallback)audio_callback, .userdata = ctx,
	}, &ctx->spec, 0);
	DRIFT_ASSERT_WARN(ctx->id, "Failed to initialize audio: %s", SDL_GetError());
	
	return ctx;
}

void DriftAudioContextFree(DriftAudioContext* ctx){
	SDL_CloseAudioDevice(ctx->id);
	SDL_DestroyMutex(ctx->sources.mutex);
	DriftArrayFree(ctx->im_samplers);
	DriftDealloc(DriftSystemMem, ctx, sizeof(*ctx));
}

void DriftAudioBusSetActive(DriftAudioBusID bus, bool active){
	DRIFT_ASSERT_HARD(bus < _DRIFT_BUS_COUNT, "Invalid audio bus id.");
	APP->audio->bus_active[bus] = active;
}

void DriftAudioStartMusic(void){
	DriftAudioContext* ctx = APP->audio;
	
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

void DriftAudioPause(bool state){
	SDL_PauseAudioDevice(APP->audio->id, state);
}

void DriftAudioSetParams(float master_volume, float music_volume, float effects_volume){
	APP->audio->master_gain = vol_to_gain(master_volume);
	APP->audio->music_gain = vol_to_gain(music_volume);
	APP->audio->effects_gain = vol_to_gain(effects_volume);
}

void DriftAudioSetReverb(float dry, float wet, float decay, float cutoff){
	DriftAudioContext* ctx = APP->audio;
	ctx->reverb.dry = dry;
	ctx->reverb.wet = wet;
	ctx->reverb.roomsize = decay;
	ctx->reverb.damp = cutoff;
}

typedef struct {
	const char** names;
	DriftAudioSample* sample_bank;
} LoadSampleContext;

// TODO this leaks samples when hot loading I guess?
static void load_sample(tina_job* job){
	TracyCZoneN(ZONE_LOAD, "Load Sample", true);
	LoadSampleContext* ctx = tina_job_get_description(job)->user_data;
	uint idx = tina_job_get_description(job)->user_idx;
	if(idx == 0) goto finish;
	
	const char* name = ctx->names[idx];
	DriftData data = DriftAssetLoad(DriftSystemMem, name);
	DRIFT_ASSERT(data.ptr, "no sample");
	
	int err = 0;
	stb_vorbis* v = stb_vorbis_open_memory(data.ptr, data.size, &err, NULL);
	DRIFT_ASSERT(v, "bad samples");
	DRIFT_ASSERT(v->channels == 1, "bad channels '%s'", name);
	DRIFT_ASSERT(v->sample_rate == 44100, "bad rate '%s'", name);
	
	uint sample_count = stb_vorbis_stream_length_in_samples(v);
	float* samples = DRIFT_ARRAY_NEW(DriftSystemMem, sample_count, typeof(*samples));
	uint decoded = stb_vorbis_get_samples_float_interleaved(v, 1, samples, sample_count);
	DRIFT_ASSERT(sample_count == decoded, "wrong length");
	
	ctx->sample_bank[idx] = (DriftAudioSample){.samples = samples, .length = sample_count};
	finish: TracyCZoneEnd(ZONE_LOAD);
}

void DriftAudioLoadSamples(tina_job* job, const char* names[], uint count){
	DriftAudioContext* ctx = APP->audio;
	ctx->sample_bank = DRIFT_ARRAY_NEW(DriftSystemMem, count, DriftAudioSample);
	
	LoadSampleContext load_ctx = {.names = names, .sample_bank = ctx->sample_bank};
	tina_job_description desc = {.name = "JobLoadSample", .func = load_sample, .user_data = &load_ctx, .queue_idx = DRIFT_JOB_QUEUE_WORK};
	tina_job_description jobs[count];
	while(desc.user_idx < count) jobs[desc.user_idx] = desc, desc.user_idx++;
	
	tina_group group = {};
	tina_scheduler_enqueue_batch(tina_job_get_scheduler(job), jobs, count, &group, 0);
	tina_job_wait(job, &group, 0);
}

#define SAMPLER_FRACT_BITS 32

typedef struct {
	float* samples;
	u64 cursor, end;
	DriftAudioParams params;
	float prev_gain;
} SamplerData;

_Static_assert(sizeof(SamplerData) < MAX_AUDIO_SOURCE_DATA_SIZE, "SamplerData is too big.");

static bool decode_sampler( float* stereo_frames, size_t frame_count, void* data){
	SamplerData *sampler = data;
	
	u64 cursor_inc = (u64)(sampler->params.pitch*(1ull << SAMPLER_FRACT_BITS));
	float gain_inc = (sampler->params.gain - sampler->prev_gain)/frame_count;
	for(uint i = 0; i < frame_count; i++){
		if(sampler->cursor >= sampler->end){
			if(sampler->params.loop){
				sampler->cursor -= sampler->end;
			} else {
				return true;
			}
		}
		
		float s = sampler->prev_gain*sampler->samples[sampler->cursor >> SAMPLER_FRACT_BITS];
		sampler->cursor += cursor_inc;
		sampler->prev_gain += gain_inc;
		
		(*stereo_frames++) += s*DriftClamp(1 - sampler->params.pan, 0, 1);
		(*stereo_frames++) += s*DriftClamp(1 + sampler->params.pan, 0, 1);
	}
	
	// Cancel the sampler if it's gain is 0.
	return sampler->params.gain == 0;
}

DriftAudioSampler DriftAudioPlaySample(DriftAudioBusID bus, DriftSFX sfx, DriftAudioParams params){
	DriftAudioContext* ctx = APP->audio;
	
	if(params.pitch == 0.0f) params.pitch = 1.0f;
	DRIFT_ASSERT(params.gain >= 0, "Audio gain cannot be negative.")
	DRIFT_ASSERT(params.pitch >= 0, "Audio pitch cannot be negative.")
	DRIFT_ASSERT(-1 <= params.pan && params.pan <= 1, "Audio pan must be in range [-1, 1].")
	
	DriftAudioSample* sample = ctx->sample_bank + sfx;
	SamplerData data = {
		.samples = sample->samples, .params = params, .prev_gain = params.gain,
		.cursor = 0, .end = (u64)sample->length << SAMPLER_FRACT_BITS,
	};
	
	SDL_LockMutex(ctx->sources.mutex);
	DriftAudioSourceID source = audio_source_aquire(&ctx->sources);
	if(source.id){
		uint source_idx = source.id % MAX_AUDIO_SOURCES;
		AudioSourceData* source_data = ctx->sources.data + source_idx;
		source_data->bus_id = bus;
		source_data->func = decode_sampler;
		memcpy(source_data->bytes, &data, sizeof(data));
		
		while(true){
			uint remain = (ctx->sources.queue.tail - ctx->sources.queue.head) % MAX_AUDIO_SOURCES;
			if(remain != 1){
				ctx->sources.queue.arr[ctx->sources.queue.head % MAX_AUDIO_SOURCES] = source;
				atomic_thread_fence(memory_order_release);
				ctx->sources.queue.head++;
				break;
			} else {
				DRIFT_LOG("Audio queue is full!");
			}
		}
	}
	SDL_UnlockMutex(ctx->sources.mutex);
	
	return (DriftAudioSampler){source};
}


void DriftAudioSamplerSetParams(DriftAudioSampler sampler, DriftAudioParams params){
	DriftAudioContext* ctx = APP->audio;
	
	if(params.pitch == 0.0f) params.pitch = 1.0f;
	DRIFT_ASSERT(params.gain >= 0, "Audio gain cannot be negative.")
	DRIFT_ASSERT(params.pitch >= 0, "Audio pitch cannot be negative.")
	DRIFT_ASSERT(-1 <= params.pan && params.pan <= 1, "Audio pan must be in range [-1, 1].")
	
	uint source_idx = audio_source_active(&ctx->sources, sampler.source);
	if(source_idx){
		SamplerData* data = (SamplerData*)ctx->sources.data[source_idx].bytes;
		data->params = params;
	}
}

bool DriftAudioSourceActive(DriftAudioSourceID source){
	return audio_source_active(&APP->audio->sources, source) != 0;
}

void DriftImAudioSet(DriftAudioBusID bus, uint sfx, DriftAudioSampler* sampler, DriftAudioParams params){
	DriftAudioContext* ctx = APP->audio;
	DRIFT_ARRAY_FOREACH(ctx->im_samplers, im){
		if(im->sampler.source.id == sampler->source.id){
			im->params = params;
			return;
		}
	}
	
	*sampler = DriftAudioPlaySample(bus, sfx, params);
	DRIFT_ARRAY_PUSH(ctx->im_samplers, ((DriftImAudioSampler){.sampler = *sampler, .params = params}));
}

void DriftImAudioUpdate(){
	DriftAudioContext* ctx = APP->audio;
	
	DRIFT_ARRAY(DriftImAudioSampler) samplers = ctx->im_samplers;
	DriftArray* header = DriftArrayHeader(samplers);
	for(uint i = header->count - 1; i < header->count; i--){
		DriftAudioSamplerSetParams(samplers[i].sampler, samplers[i].params);
		if(samplers[i].params.gain == 0) samplers[i] = samplers[--header->count];
		samplers[i].params.gain = 0;
	}
}
