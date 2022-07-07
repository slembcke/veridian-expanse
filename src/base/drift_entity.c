#include <string.h>

#include "drift_types.h"
#include "drift_util.h"
#include "drift_mem.h"
#include "drift_entity.h"

_Static_assert(DRIFT_ENTITY_INDEX_BITS + DRIFT_ENTITY_GENERATION_BITS + DRIFT_ENTITY_TAG_BITS <= 8*sizeof(DriftEntity), "Too many Entity bits.");
_Static_assert(DRIFT_ENTITY_INDEX_BITS <= 24, "Sanity check on entity array size.");
_Static_assert(DRIFT_ENTITY_GENERATION_BITS <= 8, "Generation needs to fit into a u8.");

#define DRIFT_ENTITY_SET_INDEX_MASK (DRIFT_ENTITY_SET_INDEX_COUNT - 1)

DriftEntitySet* DriftEntitySetInit(DriftEntitySet* set){
	memset(set, 0x00, sizeof(*set));
	// Pre-allocate the null entity.
	DriftEntitySetAquire(set, 0);
	return set;
}

DriftEntity DriftEntitySetAquire(DriftEntitySet* set, uint tag){
	uint pool_size = (set->pool_head - set->pool_tail) & DRIFT_ENTITY_SET_INDEX_MASK;
	if(pool_size < DRIFT_ENTITY_SET_MIN_FREE_INDEXES){
		// Start tracking a new index.
		uint idx = set->entity_count++;
		DRIFT_ASSERT_HARD(idx < DRIFT_ENTITY_SET_INDEX_COUNT, "Entity index overflow.");
		return DriftEntityMake(idx, 0, tag);
	} else {
		// A free index was available, reuse it.
		uint idx = set->pooled_indexes[set->pool_tail++ & DRIFT_ENTITY_SET_INDEX_MASK];
		return DriftEntityMake(idx, set->generations[idx], tag);
	}
}

void DriftEntitySetRetire(DriftEntitySet* set, DriftEntity entity){
	DRIFT_ASSERT_WARN(DriftEntitySetCheck(set, entity), "Trying to Retire an entity that does not exist.");
	
	uint idx = DriftEntityIndex(entity);
	set->generations[idx]++;
	set->pooled_indexes[set->pool_head++ & DRIFT_ENTITY_SET_INDEX_MASK] = idx;
}

#if DRIFT_DEBUG
void unit_test_entity(void){
	{
		DriftEntity entity = DriftEntityMake(123, 45, 6);
		DRIFT_ASSERT(DriftEntityIndex(entity) == 123, "Incorrect index.");
		DRIFT_ASSERT(DriftEntityGeneration(entity) == 45, "Incorrect generation.");
		DRIFT_ASSERT(DriftEntityTag(entity) == 6, "Incorrect tag.");
	}{
		DriftEntity entity = DriftEntityMake(~0, ~0, ~0);
		DRIFT_ASSERT(DriftEntityIndex(entity) == (1 << DRIFT_ENTITY_INDEX_BITS) - 1, "Incorrect index.");
		DRIFT_ASSERT(DriftEntityGeneration(entity) == (1 << DRIFT_ENTITY_GENERATION_BITS) - 1, "Incorrect generation.");
		DRIFT_ASSERT(DriftEntityTag(entity) == (1 << DRIFT_ENTITY_TAG_BITS) - 1, "Incorrect tag.");
	}
	
	DRIFT_LOG("Entity tests passed.");
	
	static DriftEntitySet entities = {};
	DriftEntitySetInit(&entities);
	
	uint generations = (1 << DRIFT_ENTITY_GENERATION_BITS);
	uint tag = 0;//(1 << DRIFT_ENTITY_TAG_BITS) - 1;

	DriftEntity zero = {0};
	DRIFT_ASSERT(!DriftEntitySetCheck(&entities, zero), "Zero entity should not ever be active.");
	
	for(uint g = 0; g < generations; g++){
		for(uint idx = 1; idx < DRIFT_ENTITY_SET_MIN_FREE_INDEXES + 1; idx++){
			DriftEntity entity = DriftEntitySetAquire(&entities, tag);
			DRIFT_ASSERT(DriftEntityIndex(entity) == idx, "Incorrect index.");
			DRIFT_ASSERT(DriftEntityGeneration(entity) == g, "Incorrect generation.");
			DRIFT_ASSERT(DriftEntityTag(entity) == tag, "Incorrect tag.");
			DRIFT_ASSERT(DriftEntitySetCheck(&entities, entity), "Incorrect live state.");
			
			DriftEntitySetRetire(&entities, entity);
			DRIFT_ASSERT(!DriftEntitySetCheck(&entities, entity), "Incorrect live state.");
		}
	}
	
	// Expected to wrap around finally.
	DriftEntity entity = DriftEntitySetAquire(&entities, 0);
	DRIFT_ASSERT(entity.id == 1, "Incorrect id.");
	
	DRIFT_LOG("EntitySet tests passed.");
}
#endif
