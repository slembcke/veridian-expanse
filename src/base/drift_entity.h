#pragma once

// http://bitsquid.blogspot.com/2014/08/building-data-oriented-entity-system.html
#define DRIFT_ENTITY_INDEX_BITS 16u
#define DRIFT_ENTITY_GENERATION_BITS 8u
#define DRIFT_ENTITY_TAG_BITS 4u

typedef struct {
	u32 id;
} DriftEntity;

static inline DriftEntity DriftEntityMake(uint index, uint generation, uint tag){
	uint32_t id = tag;
	id = (id << DRIFT_ENTITY_GENERATION_BITS) | generation;
	id = (id << DRIFT_ENTITY_INDEX_BITS) | index;
	return (DriftEntity){id};
}

static inline uint _DriftEntityShiftMask(DriftEntity e, uint shift, uint bits){return (e.id >> shift) & ((1 << bits) - 1);}
static inline uint DriftEntityIndex(DriftEntity e){return _DriftEntityShiftMask(e, 0, DRIFT_ENTITY_INDEX_BITS);}
static inline uint DriftEntityGeneration(DriftEntity e){return _DriftEntityShiftMask(e, DRIFT_ENTITY_INDEX_BITS, DRIFT_ENTITY_GENERATION_BITS);}
static inline uint DriftEntityTag(DriftEntity e){return _DriftEntityShiftMask(e, DRIFT_ENTITY_INDEX_BITS + DRIFT_ENTITY_GENERATION_BITS, DRIFT_ENTITY_TAG_BITS);}

#define DRIFT_ENTITY_FORMAT "e%05X"

#define DRIFT_ENTITY_SET_INDEX_COUNT (1 << DRIFT_ENTITY_INDEX_BITS)
#define DRIFT_ENTITY_SET_MIN_FREE_INDEXES 1024

typedef struct {
	uint entity_count, pool_head, pool_tail;
	u32 pooled_indexes[DRIFT_ENTITY_SET_INDEX_COUNT];
	u8 generations[DRIFT_ENTITY_SET_INDEX_COUNT];
} DriftEntitySet;

DriftEntitySet* DriftEntitySetInit(DriftEntitySet* set);

// Add a new 
DriftEntity DriftEntitySetAquire(DriftEntitySet* set, uint tag);
void DriftEntitySetRetire(DriftEntitySet* set, DriftEntity entity);

static inline bool DriftEntitySetCheck(DriftEntitySet *set, DriftEntity entity){
	uint idx = DriftEntityIndex(entity);
	return idx != 0 && set->generations[idx] == DriftEntityGeneration(entity);
}
