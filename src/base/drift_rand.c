#include <stdlib.h>

#include "drift_types.h"
#include "drift_math.h"
#include "drift_rand.h"

DriftVec2 DriftRandomInUnitCircle(){
	float theta = (float)(2*M_PI)*(float)rand()/(float)RAND_MAX;
	float radius = sqrtf((float)rand()/(float)RAND_MAX);
	return (DriftVec2){radius*cosf(theta), radius*sinf(theta)};
}
