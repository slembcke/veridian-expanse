enum {
	DRIFT_SPRITE_MODE_NORMAL,
	DRIFT_SPRITE_MODE_FLAT,
	DRIFT_SPRITE_MODE_EMISSIVE,
};

typedef struct {
	struct {u8 l, b, r, t;} bounds;
	struct {u8 x, y;} anchor;
	u16 layer;
} DriftSpriteFrame;

typedef struct {
	DriftSpriteFrame frame;
	DriftRGBA8 color;
	DriftAffine matrix;
	float z, _;
} DriftSprite;

typedef struct {
	DriftSpriteFrame frame;
	DriftVec4 color;
	DriftAffine matrix;
	float radius;
	bool shadow_caster;
} DriftLight;

extern const DriftSpriteFrame DRIFT_SPRITE_FRAMES[];

static inline DriftAffine DriftSpriteFrameMatrix(DriftAffine matrix, DriftSpriteFrame frame){
	DriftVec2 anchor = {(s8)frame.anchor.x, (s8)frame.anchor.y};
	anchor.x /= frame.bounds.r - frame.bounds.l;
	anchor.y /= frame.bounds.t - frame.bounds.b;
	matrix.x += 2*(matrix.a*anchor.x + matrix.c*anchor.y);
	matrix.y += 2*(matrix.b*anchor.x + matrix.d*anchor.y);
	
	return matrix;
}

static inline DriftSprite DriftSpriteMake(uint frame, DriftRGBA8 color, DriftAffine matrix){
	return (DriftSprite){.frame = DRIFT_SPRITE_FRAMES[frame], .color = color, .matrix = matrix};
}
static inline void DriftSpritePush(DriftSprite* arr[], uint frame, DriftRGBA8 color, DriftAffine matrix){
	*(*arr)++ = DriftSpriteMake(frame, color, matrix);
}

static inline DriftLight DriftLightMake(bool shadows, uint frame, DriftVec4 color, DriftAffine matrix, float radius){
	return (DriftLight){.frame = DRIFT_SPRITE_FRAMES[frame], .color = color, .matrix = matrix, .shadow_caster = shadows, .radius = radius};
}
static inline void DriftLightPush(DriftLight* arr[], bool shadows, uint frame, DriftVec4 color, DriftAffine matrix, float radius){
	*(*arr)++ = DriftLightMake(shadows, frame, color, matrix, radius);
}

void DriftGradientMap(u8* pixels, uint image_w, uint image_h, uint layer);

enum {
	DRIFT_ATLAS_TEXT,
	DRIFT_ATLAS_INPUT,
	DRIFT_ATLAS_LIGHTS,
	DRIFT_ATLAS_PLAYER,
	DRIFT_ATLAS_PLAYERG,
	DRIFT_ATLAS_BIOME,
	DRIFT_ATLAS_LIGHT,
	DRIFT_ATLAS_LIGHTG,
	DRIFT_ATLAS_RADIO,
	DRIFT_ATLAS_RADIOG,
	DRIFT_ATLAS_CRYO,
	DRIFT_ATLAS_CRYOG,
	DRIFT_ATLAS0,
	DRIFT_ATLAS0_FX,
	DRIFT_ATLAS1,
	DRIFT_ATLAS1_FX,
	_DRIFT_ATLAS_COUNT,
};

typedef enum {
	#include "sprite_enums.inc"
	_DRIFT_SPRITE_COUNT,
} DriftSpriteEnum;

enum {
	_DRIFT_SPRITE_FLAME_COUNT = 8,
};
