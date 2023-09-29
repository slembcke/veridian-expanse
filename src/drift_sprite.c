/*
This file is part of Veridian Expanse.

Veridian Expanse is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

Veridian Expanse is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with Veridian Expanse. If not, see <https://www.gnu.org/licenses/>.
*/

#include "drift_game.h"

const DriftFrame DRIFT_FRAMES[] = {
	[DRIFT_SPRITE_NONE] = {.layer = DRIFT_ATLAS_MISC, .bounds = {0x10, 0x60, 0x1F, 0x6F}, .anchor = {8, 8}},
	[DRIFT_SPRITE_SCAN_IMAGE] = {.layer = DRIFT_ATLAS_SCAN, .bounds = {0x00, 0x00, 0xFF, 0x3F}, .anchor = {127, 32}},
	#include "sprite_defs.inc"
};

typedef float KernelFunc(float);

static float KernelBox(float x){return 1;}
static float KernelTriangle(float x){return 1 - fabsf(x);}
static float KernelBlackman(float x){
	const float a0 = 0.4265907136715391f;
	const float a1 = 0.4965606190885641f;
	const float a2 = 0.07684866723989682f;
	return a0 + a1*cosf((float)M_PI*x) + a2*cosf(2*(float)M_PI*x);
}

typedef struct {
	float radius, strength;
	KernelFunc* func;
} FilterParams;

typedef struct {
	uint frame;
	FilterParams shape, detail;
} GradientInfo;

static const GradientInfo GRADIENT_INFO[] = {
	{.frame = DRIFT_SPRITE_CRASHED_SHIP, .shape = {32, 32, KernelBlackman}, .detail = {1, 2, KernelBlackman}},
	{.frame = DRIFT_SPRITE_HULL, .shape = {48, 64, KernelBlackman}, .detail = {1, 4, KernelBlackman}},
	{.frame = DRIFT_SPRITE_STRUT, .shape = {2, 1.2f, KernelBlackman}, .detail = {0.5, 0.0, KernelBlackman}},
	{.frame = DRIFT_SPRITE_GRABBER_MOUNT, .shape = {2, 1.2f, KernelBlackman}, .detail = {0.5, 0.0, KernelBlackman}},
	{.frame = DRIFT_SPRITE_GRABARM0, .shape = {2, 1.2f, KernelBlackman}, .detail = {0.5, 0.0, KernelBlackman}},
	{.frame = DRIFT_SPRITE_GRABARM1, .shape = {2, 1.2f, KernelBlackman}, .detail = {0.5, 0.0, KernelBlackman}},
	{.frame = DRIFT_SPRITE_GRABARM2, .shape = {2, 1.2f, KernelBlackman}, .detail = {0.5, 0.0, KernelBlackman}},
	{.frame = DRIFT_SPRITE_GRIPPER, .shape = {2, 1.2f, KernelBlackman}, .detail = {0.5, 0.0, KernelBlackman}},
	{.frame = DRIFT_SPRITE_NACELLE, .shape = {4, 2, KernelBlackman}, .detail = {0.5, 1.0, KernelBlackman}},
	{.frame = DRIFT_SPRITE_GUN, .shape = {1, 4, KernelBlackman}, .detail = {0.5, 0.0, KernelBlackman}},
	{.frame = DRIFT_SPRITE_LASER_EXTENSION_PADDED, .shape = {2, 8, KernelBlackman}, .detail = {0.5, 1.0, KernelBlackman}},
	{.frame = DRIFT_SPRITE_SCANNER, .shape = {10, 4, KernelBlackman}, .detail = {1.5f, 2.0, KernelBlackman}},
	{.frame = DRIFT_SPRITE_POWER_NODE, .shape = {4, 2, KernelBlackman}, .detail = {1, 1, KernelBlackman}},
	
	{.frame = DRIFT_SPRITE_DRONE_CHASSIS, .shape = {8, 2, KernelBlackman}, .detail = {1, 1, KernelBlackman}},
	{.frame = DRIFT_SPRITE_DRONE_HATCH, .shape = {8, 2, KernelBlackman}, .detail = {1, 1, KernelBlackman}},
	
	{.frame = DRIFT_SPRITE_SCRAP00, .shape = {1.5, 2, KernelBlackman}, .detail = {1.5f, 5.0f, KernelBlackman}},
	{.frame = DRIFT_SPRITE_ADVANCED_SCRAP, .shape = {1.5, 2, KernelBlackman}, .detail = {1.5f, 5.0f, KernelBlackman}},

	{.frame = DRIFT_SPRITE_LUMIUM   , .shape = {3, 3.2f, KernelBlackman}, .detail = {1.25, 3.2f, KernelBlackman}},
	{.frame = DRIFT_SPRITE_FLOURON  , .shape = {3, 3.2f, KernelBlackman}, .detail = {1.25, 3.2f, KernelBlackman}},
	{.frame = DRIFT_SPRITE_FUNGICITE, .shape = {3, 3.2f, KernelBlackman}, .detail = {1.25, 3.2f, KernelBlackman}},
	{.frame = DRIFT_SPRITE_MORPHITE , .shape = {3, 3.2f, KernelBlackman}, .detail = {1.25, 3.2f, KernelBlackman}},

	{.frame = DRIFT_SPRITE_HIVE_POD, .shape = {16, 16, KernelBlackman}, .detail = {1, 4, KernelBlackman}},
	{.frame = DRIFT_SPRITE_HIVE_WART, .shape = {8, 4, KernelBlackman}, .detail = {1, 2, KernelBlackman}},
	{-1}, // terminator.
};

typedef struct {
	DriftMem* mem;
	uint w, h;
	float* samples;
	size_t samples_size;
} Buffer;

static void BufferInit(Buffer* buffer, DriftMem* mem, uint w, uint h){
	size_t size = w*h*sizeof(*buffer->samples);
	size = 256*256*4;
	*buffer = (Buffer){.mem = mem, .w = w, .h = h, .samples = DriftAlloc(DriftSystemMem, size), .samples_size = size};
}

static void BufferDestroy(Buffer* buffer){
	DriftDealloc(buffer->mem, buffer->samples, buffer->samples_size);
}

static void BufferBlitFromU8(Buffer* dst, u8* src, size_t stride, size_t row_len){
	for(uint y = 0; y < dst->h; y++){
		u8* row = src + y*row_len;
		for(uint x = 0; x < dst->w; x++){
			dst->samples[x + y*dst->w] = row[x*stride];
		}
	}
}

static void BufferBlitIntoU8(Buffer* src0, Buffer* src1, u8* dst, size_t stride, size_t row_len){
	for(uint y = 0; y < src0->h; y++){
		u8* row = dst + y*row_len;
		for(uint x = 0; x < src0->w; x++){
			uint idx = x + y*src0->w;
			row[x*stride] = (u8)DriftClamp(src0->samples[idx] + src1->samples[idx] + 127.5f, 0, 255);
		}
	}
}

static uint Clamp(uint x, uint max){return (x < 0 ? 0 : (x > max ? max : x));}

static float BufferSampleWrap(Buffer* buf, uint x, uint y){return buf->samples[x%buf->w + y*buf->w];}
static float BufferSampleClamp(Buffer* buf, uint x, uint y){return buf->samples[Clamp(x, buf->w) + y*buf->w];}
static float BufferSampleBorder(Buffer* buf, uint x, uint y){return x < buf->w ? buf->samples[x + y*buf->w] : 0;}

#define KERNEL_MAX_SIZE 512

typedef struct {
	uint len;
	float samples[KERNEL_MAX_SIZE];
} FilterKernel;

static FilterKernel MakeKernel(float radius, KernelFunc func){
	// Generate.
	FilterKernel k = {.len = 2*(uint)floor(radius) + 1};
	DRIFT_ASSERT(k.len < KERNEL_MAX_SIZE, "Kernel too big.");
	for(uint i = 0; i < k.len; i++){
		k.samples[i] = func((i - floorf(radius))/radius);
	}
	
	// Normalize.
	float sum = 0;
	for(uint i = 0; i < k.len; i++) sum += k.samples[i];
	float coef = 1/sum;
	for(uint i = 0; i < k.len; i++) k.samples[i] *= coef;
	
	return k;
}

static void SeparableConvolvePass(Buffer* dst, Buffer* src, const FilterKernel* kernel){
	uint w = src->w, h = src->h;
	uint offset = kernel->len/2;
	DRIFT_ASSERT(dst->w == src->h && dst->h == src->w, "Output dimensions don't match.");
	
	for(uint y = 0; y < src->h; y++){
		for(uint x = 0; x < src->w; x++){
			float sum = 0;
			for(uint z = 0; z < kernel->len; z++){
				sum += BufferSampleBorder(src, x + z - offset, y)*kernel->samples[z];
			}
			dst->samples[y + x*h] = sum;
		}
	}
}

static void ConvolveXY(Buffer* dst, Buffer* src, const FilterKernel* k_x, const FilterKernel* k_y){
	DRIFT_ASSERT(dst->w == src->w && dst->h == src->h, "Output dimensions don't match.");
	Buffer tmp; BufferInit(&tmp, DriftSystemMem, dst->h, dst->w);
	SeparableConvolvePass(&tmp, src, k_x);
	SeparableConvolvePass(dst, &tmp, k_y);
	BufferDestroy(&tmp);
}

static DriftFrame LayerFrame(const GradientInfo *grad){
	return (DriftFrame){.layer = grad->frame - _DRIFT_SPRITE_COUNT, .bounds = {0x00, 0x00, 0xFF, 0xFF}};
}

void DriftGradientMap(u8* pixels, uint image_w, uint image_h, uint layer){
	size_t row_len = 4*image_w;
	
	for(const GradientInfo* grad = GRADIENT_INFO; grad->frame != ~0u; grad++){
		DriftFrame frame = grad->frame < _DRIFT_SPRITE_COUNT ? DRIFT_FRAMES[grad->frame] : LayerFrame(grad);
		if(frame.layer + 1u != layer) continue;
		
		uint w = frame.bounds.r - frame.bounds.l + 1;
		uint h = frame.bounds.t - frame.bounds.b + 1;
		u8* origin = pixels + 4*(frame.bounds.l + frame.bounds.b*image_w);
		
		FilterKernel sobel0 = {.len = 3, .samples = { 0.25, 0.5, 0.25}};
		FilterKernel sobel1 = {.len = 3, .samples = {-0.5f*grad->shape.strength, 0, 0.5f*grad->shape.strength}};
		FilterKernel sobel2 = {.len = 3, .samples = {-0.5f*grad->detail.strength, 0, 0.5f*grad->detail.strength}};
		
		// Rough shape from red.
		Buffer z0; BufferInit(&z0, DriftSystemMem, w, h);
		BufferBlitFromU8(&z0, origin + 0, 4, row_len);
		
		// Partial derivatives using sobel filter.
		Buffer dzdx0; BufferInit(&dzdx0, DriftSystemMem, w, h);
		Buffer dzdy0; BufferInit(&dzdy0, DriftSystemMem, w, h);
		ConvolveXY(&dzdx0, &z0, &sobel1, &sobel0);
		ConvolveXY(&dzdy0, &z0, &sobel0, &sobel1);
		BufferDestroy(&z0);
		
		// Smooth using user filter.
		FilterKernel filter_shape = MakeKernel(grad->shape.radius, grad->shape.func);
		ConvolveXY(&dzdx0, &dzdx0, &filter_shape, &filter_shape);
		ConvolveXY(&dzdy0, &dzdy0, &filter_shape, &filter_shape);
		
		// Detail in green.
		Buffer z1; BufferInit(&z1, DriftSystemMem, w, h);
		BufferBlitFromU8(&z1, origin + 1, 4, row_len);
		
		// Partial derivatives using sobel filter.
		Buffer dzdx1; BufferInit(&dzdx1, DriftSystemMem, w, h);
		Buffer dzdy1; BufferInit(&dzdy1, DriftSystemMem, w, h);
		ConvolveXY(&dzdx1, &z1, &sobel2, &sobel0);
		ConvolveXY(&dzdy1, &z1, &sobel0, &sobel2);
		BufferDestroy(&z1);
		
		// Smooth using user filter.
		FilterKernel filter_detail = MakeKernel(grad->detail.radius, grad->detail.func);
		ConvolveXY(&dzdx1, &dzdx1, &filter_detail, &filter_detail);
		ConvolveXY(&dzdy1, &dzdy1, &filter_detail, &filter_detail);
		
		BufferBlitIntoU8(&dzdx0, &dzdx1, origin + 0, 4, row_len);
		BufferBlitIntoU8(&dzdy0, &dzdy1, origin + 1, 4, row_len);
		
		BufferDestroy(&dzdx0); BufferDestroy(&dzdx1);
		BufferDestroy(&dzdy0); BufferDestroy(&dzdy1);
	}
}
