#pragma once

typedef struct {
	uintptr_t default_value;
	
	DriftTable table;
	u8* infobytes;
	uintptr_t* keys;
	uintptr_t* values;
} DriftMap;

DriftMap* DriftMapInit(DriftMap* map, DriftMem* mem, const char* name, size_t min_capacity);
void DriftMapDestroy(DriftMap* map);

uintptr_t DriftMapInsert(DriftMap *map, uintptr_t key, uintptr_t value);
uintptr_t DriftMapFind(DriftMap const* map, uintptr_t key);
uintptr_t DriftMapRemove(DriftMap* map, uintptr_t key);
static inline bool DriftMapActiveIndex(DriftMap const* map, uint idx){return map->infobytes[idx];}
