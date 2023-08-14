/*
This file is part of Veridian Expanse.

Veridian Expanse is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

Veridian Expanse is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with Veridian Expanse. If not, see <https://www.gnu.org/licenses/>.
*/

#include <stdlib.h>

#define LIFFT_IMPLEMENTATION
#include "drift_base.h"

void lifft_multiply_accumulate(lifft_complex_t* acc, lifft_complex_t* x, lifft_complex_t* y, uint size){
	for(uint i = 0; i < size; i++) acc[i] = lifft_cadd(acc[i], lifft_cmul(x[i], y[i]));
}

// #undef DRIFT_RAND_MAX
// #define DRIFT_RAND_MAX RAND_MAX
// u32 DriftRand32(DriftRandom _rand[1]){return rand();}

// https://nullprogram.com/blog/2017/09/21/
u32 DriftRand32(DriftRandom rand[1]){
	u64 m = 0x9b60933458e17d7d;
	u64 a = 0xd737232eeccdf7ed;
	rand->state = rand->state * m + a;
	int shift = 29 - (rand->state >> 61);
	return rand->state >> shift;
}

float DriftRandomUNorm(DriftRandom rand[1]){return (float)DriftRand32(rand)/(float)DRIFT_RAND_MAX;}
float DriftRandomSNorm(DriftRandom rand[1]){return 2*DriftRandomUNorm(rand) - 1;}

DriftVec2 DriftRandomOnUnitCircle(DriftRandom rand[1]){
	return DriftVec2ForAngle((float)M_PI*DriftRandomSNorm(rand));
}

DriftVec2 DriftRandomInUnitCircle(DriftRandom rand[1]){
	float theta = (float)M_PI*DriftRandomSNorm(rand);
	float radius = sqrtf(DriftRandomUNorm(rand));
	return DriftVec2Mul(DriftVec2ForAngle(theta), radius);
}

DriftReservoir DriftReservoirMake(DriftRandom state[1]){
	return (DriftReservoir){.rand = DriftRandomUNorm(state)};
}

bool DriftReservoirSample(DriftReservoir* ctx, float weight){
	ctx->sum += weight;
	float value = weight/(ctx->sum + FLT_MIN);
	if(ctx->rand < value){
		ctx->rand /= value;
		return true;
	} else {
		ctx->rand = (ctx->rand - value)/(1 - value);
		return false;
	}
}

#if DRIFT_DEBUG
void unit_test_math(void){
	DRIFT_LOG("Math tests passed.");
}
#endif
