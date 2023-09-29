/*
This file is part of Veridian Expanse.

Veridian Expanse is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

Veridian Expanse is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with Veridian Expanse. If not, see <https://www.gnu.org/licenses/>.
*/

enum {
	DRIFT_SPRITE_MODE_NORMAL,
	DRIFT_SPRITE_MODE_FLAT,
	DRIFT_SPRITE_MODE_EMISSIVE,
};

typedef struct { // 9 bytes
	struct {u8 l, b, r, t;} bounds; // 4 bytes
	struct {u8 x, y;} anchor; // 2 bytes
	u8 layer, glow, shiny; // 3 bytes
} DriftFrame;

typedef struct {
	DriftAffine matrix; // 24 bytes
	DriftRGBA8 color; // 4 bytes
	DriftFrame frame; // 9 bytes
	u8 z;
} DriftSprite;

typedef struct {
	DriftAffine matrix; // 24 bytes
	DriftVec4 color; // 16 bytes
	DriftFrame frame; // 9 bytes
	float radius; // 4 bytes
} DriftLight;

extern const DriftFrame DRIFT_FRAMES[];

static inline DriftSprite DriftSpriteMake(uint frame, DriftRGBA8 color, DriftAffine matrix){
	return (DriftSprite){.frame = DRIFT_FRAMES[frame], .color = color, .matrix = matrix};
}
static inline void DriftSpritePush(DriftSprite* arr[], uint frame, DriftRGBA8 color, DriftAffine matrix){
	*(*arr)++ = DriftSpriteMake(frame, color, matrix);
}

static inline DriftLight DriftLightMake(uint frame, DriftVec4 color, DriftAffine matrix, float radius){
	return (DriftLight){.frame = DRIFT_FRAMES[frame], .color = color, .matrix = matrix, .radius = radius};
}
static inline void DriftLightPush(DriftLight* arr[], uint frame, DriftVec4 color, DriftAffine matrix, float radius){
	*(*arr)++ = DriftLightMake(frame, color, matrix, radius);
}

void DriftGradientMap(u8* pixels, uint image_w, uint image_h, uint layer);

enum {
	DRIFT_ATLAS_UI,
	DRIFT_ATLAS_INPUT,
	DRIFT_ATLAS_LIGHTS,
	DRIFT_ATLAS_MISC,
	DRIFT_ATLAS_MISCG,
	DRIFT_ATLAS_VISIBILITY,
	DRIFT_ATLAS_BIOME,
	DRIFT_ATLAS_LIGHT,
	DRIFT_ATLAS_LIGHTG,
	DRIFT_ATLAS_RADIO,
	DRIFT_ATLAS_RADIOG,
	DRIFT_ATLAS_CRYO,
	DRIFT_ATLAS_CRYOG,
	DRIFT_ATLAS_DARK,
	DRIFT_ATLAS_DARKG,
	DRIFT_ATLAS_BLUE,
	DRIFT_ATLAS_SCAN,
	#include "atlas_enums.inc"
	_DRIFT_ATLAS_COUNT,
};

typedef enum {
	DRIFT_SPRITE_NONE,
	#include "sprite_enums.inc"
	DRIFT_SPRITE_SCAN_IMAGE,
	_DRIFT_SPRITE_COUNT,
} DriftSpriteEnum;

enum {
	_DRIFT_SPRITE_FLAME_COUNT = 8,
};
