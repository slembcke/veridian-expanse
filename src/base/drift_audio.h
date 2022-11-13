#pragma once

typedef struct DriftAudioContext DriftAudioContext;

DriftAudioContext* DriftAudioContextNew(void);
// void DriftAudioContextFree(DriftAudioContext* ctx);

void DriftAudioSetParams(DriftAudioContext* ctx, float master_volume, float music_volume);

void DriftAudioStartMusic(DriftAudioContext* ctx);
void DriftAudioPause(DriftAudioContext* ctx, bool state);

typedef struct {u32 id;} DriftAudioSourceID;
bool DriftAudioSourceActive(DriftAudioContext* ctx, DriftAudioSourceID source_id);

typedef enum {
	DRIFT_SFX_PING,
	DRIFT_SFX_HORNS,
	DRIFT_SFX_ENGINE,
	DRIFT_SFX_TEXT_BLIP,
	DRIFT_SFX_CLICK,
	DRIFT_SFX_BULLET_FIRE,
	DRIFT_SFX_BULLET_HIT,
	DRIFT_SFX_EXPLODE,
	_DRIFT_SFX_COUNT,
} DriftSFX;

void DriftAudioLoadSamples(DriftAudioContext* ctx, tina_job* job);

typedef struct {DriftAudioSourceID source;} DriftAudioSampler;
DriftAudioSampler DriftAudioPlaySample(DriftAudioContext* ctx, DriftSFX sfx, float gain, float pan, float pitch, bool loop);
void DriftSamplerSetParams(DriftAudioContext* ctx, DriftAudioSampler sampler, float gain, float pan, float pitch, bool loop);
