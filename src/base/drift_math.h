#pragma once

#include <math.h>
#include <float.h>

static inline float DriftClamp(float x, float min, float max){return fmaxf(min, fminf(x, max));}
static inline float DriftSaturate(float x){return fmaxf(0.0f, fminf(x, 1.0f));}
static inline float DriftLerpConst(float a, float b, float dt){return a + DriftClamp(b - a, -dt ,dt);}
static inline float DriftLerp(float a, float b, float t){return (1 - t)*a + t*b;}
static inline float DriftLogerp(float a, float b, float t){return a*powf(b/a, t);}
static inline float DriftHermite3(float x){return (3 - 2*x)*x*x;}
static inline float DriftHermite5(float x){return ((6*x - 15)*x + 10)*x*x*x;}

typedef struct {float x, y;} DriftVec2;
static const DriftVec2 DRIFT_VEC2_ZERO = {0, 0};
static const DriftVec2 DRIFT_VEC2_ONE = {1, 1};

static inline DriftVec2 DriftVec2Add(DriftVec2 v1, DriftVec2 v2){return (DriftVec2){v1.x + v2.x, v1.y + v2.y};}
static inline DriftVec2 DriftVec2Sub(DriftVec2 v1, DriftVec2 v2){return (DriftVec2){v1.x - v2.x, v1.y - v2.y};}
static inline DriftVec2 DriftVec2Mul(DriftVec2 v, float s){return (DriftVec2){v.x*s, v.y*s};}
static inline DriftVec2 DriftVec2FMA(DriftVec2 v1, DriftVec2 v2, float s){return (DriftVec2){v1.x + v2.x*s, v1.y + v2.y*s};}
static inline DriftVec2 DriftVec2Lerp(DriftVec2 a, DriftVec2 b, float t){return DriftVec2Add(DriftVec2Mul(a, 1 - t), DriftVec2Mul(b, t));}
static inline DriftVec2 DriftVec2Neg(DriftVec2 v){return (DriftVec2){-v.x, -v.y};}
static inline DriftVec2 DriftVec2Perp(DriftVec2 v){return (DriftVec2){-v.y, v.x};}
static inline float DriftVec2Dot(DriftVec2 v1, DriftVec2 v2){return v1.x*v2.x + v1.y*v2.y;}
static inline float DriftVec2Cross(DriftVec2 v1, DriftVec2 V2){return v1.x*V2.y - v1.y*V2.x;}
static inline float DriftVec2LengthSq(DriftVec2 v){return DriftVec2Dot(v, v);}
static inline float DriftVec2Length(DriftVec2 v){return sqrtf(DriftVec2LengthSq(v));}
static inline float DriftVec2Distance(DriftVec2 v1, DriftVec2 v2){return DriftVec2Length(DriftVec2Sub(v2, v1));}
static inline DriftVec2 DriftVec2Normalize(DriftVec2 v){return DriftVec2Mul(v, 1/(DriftVec2Length(v) + FLT_MIN));}
static inline DriftVec2 DriftVec2Clamp(DriftVec2 v, float l){return DriftVec2Mul(v, fminf(1.0f, l/(DriftVec2Length(v) + FLT_MIN)));}
static inline DriftVec2 DriftVec2LerpConst(DriftVec2 a, DriftVec2 b, float d){return DriftVec2Add(a, DriftVec2Clamp(DriftVec2Sub(b, a), d));}
static inline DriftVec2 DriftVec2Rotate(DriftVec2 v1, DriftVec2 v2){return (DriftVec2){v1.x*v2.x - v1.y*v2.y, v1.x*v2.y + v1.y*v2.x};}
static inline DriftVec2 DriftVec2RotateInv(DriftVec2 v1, DriftVec2 v2){return (DriftVec2){v1.x*v2.x + v1.y*v2.y, v1.y*v2.x - v1.x*v2.y};}

typedef struct {
	float l, b, r, t;
} DriftAABB2;

static inline bool DriftAABB2Overlap(DriftAABB2 a, DriftAABB2 b){return (a.l < b.r && b.l < a.r && a.b < b.t && b.b < a.t);}
static inline bool DriftAABB2Contains(DriftAABB2 a, DriftAABB2 b){return a.l <= b.l && b.r <= a.r && a.b <= b.b && b.t <= a.t;}
static inline bool DriftAABB2Test(DriftAABB2 a, DriftVec2 p){return a.l <= p.x && p.x <= a.r && a.b <= p.y && p.y <= a.t;}
static inline DriftAABB2 DriftAABB2Merge(DriftAABB2 a, DriftAABB2 b){return (DriftAABB2){fminf(a.l, b.l), fminf(a.b, b.b), fmaxf(a.r, b.r), fmaxf(a.t, b.t)};}
static DriftVec2 DriftAABB2Center(DriftAABB2 bb){return (DriftVec2){(bb.l + bb.r)/2, (bb.b + bb.t)/2};}

static const DriftAABB2 DRIFT_AABB2_ALL = {-INFINITY, -INFINITY, INFINITY, INFINITY};
static const DriftAABB2 DRIFT_AABB2_UNIT = {-1, -1, 1, 1};

typedef union {
	struct {float x, y, z;};
	struct {float r, g, b;};
} DriftVec3;

static inline DriftVec3 DriftVec3Add(DriftVec3 v1, DriftVec3 v2){return (DriftVec3){{v1.x + v2.x, v1.y + v2.y, v1.z + v2.z}};}
static inline DriftVec3 DriftVec3Sub(DriftVec3 v1, DriftVec3 v2){return (DriftVec3){{v1.x - v2.x, v1.y - v2.y, v1.z - v2.z}};}
static inline DriftVec3 DriftVec3Mul(DriftVec3 v, float s){return (DriftVec3){{v.x*s, v.y*s, v.z*s}};}
static inline DriftVec3 DriftVec3Lerp(DriftVec3 a, DriftVec3 b, float t){return DriftVec3Add(DriftVec3Mul(a, 1 - t), DriftVec3Mul(b, t));}
static inline float DriftVec3Dot(DriftVec3 v1, DriftVec3 v2){return v1.x*v2.x + v1.y*v2.y + v1.z*v2.z;}
static inline float DriftVec3LengthSq(DriftVec3 v){return DriftVec3Dot(v, v);}
static inline float DriftVec3Length(DriftVec3 v){return sqrtf(DriftVec3LengthSq(v));}
static inline DriftVec3 DriftVec3Clamp(DriftVec3 v, float l){return DriftVec3Mul(v, fminf(1.0f, l/hypotf(v.x, v.y)));}

typedef union {
	struct {float x, y, z, w;};
	struct {float r, g, b, a;};
} DriftVec4;

static const DriftVec4 DRIFT_VEC4_ZERO    = {{0, 0, 0, 0}};
static const DriftVec4 DRIFT_VEC4_ONE     = {{1, 1, 1, 1}};

static const DriftVec4 DRIFT_VEC4_CLEAR   = {{0, 0, 0, 0}};
static const DriftVec4 DRIFT_VEC4_BLACK   = {{0, 0, 0, 1}};
static const DriftVec4 DRIFT_VEC4_WHITE   = {{1, 1, 1, 1}};
static const DriftVec4 DRIFT_VEC4_RED     = {{1, 0, 0, 1}};
static const DriftVec4 DRIFT_VEC4_YELLOW  = {{1, 1, 0, 1}};
static const DriftVec4 DRIFT_VEC4_GREEN   = {{0, 1, 0, 1}};
static const DriftVec4 DRIFT_VEC4_CYAN    = {{0, 1, 1, 1}};
static const DriftVec4 DRIFT_VEC4_BLUE    = {{0, 0, 1, 1}};
static const DriftVec4 DRIFT_VEC4_MAGENTA = {{1, 0, 1, 1}};

static inline DriftVec4 DriftVec4Mul(DriftVec4 v, float s){return (DriftVec4){{v.x*s, v.y*s, v.z*s, v.w*s}};}

typedef struct {u8 r, g, b, a;} DriftRGBA8;
static inline DriftRGBA8 DriftRGBA8FromColor(DriftVec4 color){return (DriftRGBA8){
		(u8)(color.r*255),
		(u8)(color.g*255),
		(u8)(color.b*255),
		(u8)(color.a*255),
	};
}

static const DriftRGBA8 DRIFT_RGBA8_CLEAR   = {0x00, 0x00, 0x00, 0x00};
static const DriftRGBA8 DRIFT_RGBA8_BLACK   = {0x00, 0x00, 0x00, 0xFF};
static const DriftRGBA8 DRIFT_RGBA8_WHITE   = {0xFF, 0xFF, 0xFF, 0xFF};
static const DriftRGBA8 DRIFT_RGBA8_RED     = {0xFF, 0x00, 0x00, 0xFF};
static const DriftRGBA8 DRIFT_RGBA8_ORANGE  = {0xFF, 0x80, 0x00, 0xFF};
static const DriftRGBA8 DRIFT_RGBA8_YELLOW  = {0xFF, 0xFF, 0x00, 0xFF};
static const DriftRGBA8 DRIFT_RGBA8_GREEN   = {0x00, 0xFF, 0x00, 0xFF};
static const DriftRGBA8 DRIFT_RGBA8_CYAN    = {0x00, 0xFF, 0xFF, 0xFF};
static const DriftRGBA8 DRIFT_RGBA8_BLUE    = {0x00, 0x00, 0xFF, 0xFF};
static const DriftRGBA8 DRIFT_RGBA8_MAGENTA = {0xFF, 0x00, 0xFF, 0xFF};

typedef struct {float a, b, c, d, x, y;} DriftAffine;

static const DriftAffine DRIFT_AFFINE_ZERO = {0, 0, 0, 0, 0, 0};
static const DriftAffine DRIFT_AFFINE_IDENTITY = {1, 0, 0, 1, 0, 0};

static inline DriftAffine DriftAffineMakeTranspose(float a, float c, float x, float b, float d, float y){
	return (DriftAffine){a, b, c, d, x, y};
}

static inline DriftAffine DriftAffineMult(DriftAffine m1, DriftAffine m2){
  return DriftAffineMakeTranspose(
    m1.a*m2.a + m1.c*m2.b, m1.a*m2.c + m1.c*m2.d, m1.a*m2.x + m1.c*m2.y + m1.x,
    m1.b*m2.a + m1.d*m2.b, m1.b*m2.c + m1.d*m2.d, m1.b*m2.x + m1.d*m2.y + m1.y
  );
}

static inline DriftAffine DriftAffineInverse(DriftAffine m){
  float inv_det = 1/(m.a*m.d - m.c*m.b);
  return DriftAffineMakeTranspose(
     m.d*inv_det, -m.c*inv_det, (m.c*m.y - m.d*m.x)*inv_det,
    -m.b*inv_det,  m.a*inv_det, (m.b*m.x - m.a*m.y)*inv_det
  );
}

static inline DriftAffine DriftAffineTRS(DriftVec2 t, float r, DriftVec2 s){
	DriftVec2 rot = {cosf(r), sinf(r)};
	return DriftAffineMakeTranspose(
		 s.x*rot.x, s.y*rot.y, t.x,
		-s.x*rot.y, s.y*rot.x, t.y
	);
}

static inline DriftAffine DriftAffineOrtho(const float l, const float r, const float b, const float t){
	float sx = 2/(r - l);
	float sy = 2/(t - b);
	float tx = -(r + l)/(r - l);
	float ty = -(t + b)/(t - b);
	return DriftAffineMakeTranspose(
		sx,  0, tx,
		 0, sy, ty
	);
}


//MARK: Transform functions.

static inline DriftVec2 DriftAffineOrigin(DriftAffine t){return (DriftVec2){t.x, t.y};}

static inline DriftVec2 DriftAffineDirection(DriftAffine t, DriftVec2 p){
	return (DriftVec2){t.a*p.x + t.c*p.y, t.b*p.x + t.d*p.y};
}

static inline DriftVec2 DriftAffinePoint(DriftAffine t, DriftVec2 p){
	return (DriftVec2){t.a*p.x + t.c*p.y + t.x, t.b*p.x + t.d*p.y + t.y};
}

// static inline pvec4 DriftAffinePoint4(DriftAffine t, DriftVec2 p){
// 	pvec2 p2 = DriftAffinePoint(t, p);
// 	return (pvec4){{p2.x, p2.y, 0, 1}};
// }

typedef struct {float m[8];} DriftGPUMatrix;

static inline DriftGPUMatrix DriftAffineToGPU(DriftAffine m){
	return (DriftGPUMatrix){.m = {m.a, m.c, m.x, 0, m.b, m.d, m.y, 0}};
}

static inline bool DriftAffineVisibility(DriftAffine mvp, DriftVec2 center, DriftVec2 extent){
	// Center point in clip coordinates.
	DriftVec2 csc = DriftAffinePoint(mvp, center);
	
	// half width/height in clip space.
	float cshw = fabsf(extent.x*mvp.a) + fabsf(extent.y*mvp.c);
	float cshh = fabsf(extent.x*mvp.b) + fabsf(extent.y*mvp.d);
	
	// Check the bounds against the clip space viewport.
	return ((fabsf(csc.x) - cshw < 1) && (fabsf(csc.y) - cshh < 1));
}

// http://extremelearning.com.au/unreasonable-effectiveness-of-quasirandom-sequences/
static inline DriftVec2 DriftNoiseR2(u32 i){
	return (DriftVec2){
		(0x1p-32f)*(u32)(3242174889u*i),
		(0x1p-32f)*(u32)(2447445414u*i),
	};
}
