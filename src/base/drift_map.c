#include <stdlib.h>
#include <string.h>

#include "drift_types.h"
#include "drift_util.h"
#include "drift_mem.h"
#include "drift_table.h"
#include "drift_map.h"

// https://martin.ankerl.com/2016/09/21/very-fast-hashmap-in-c-part-2/
#define DRIFT_INDEXMAP_NOT_FOUND (~0u)
#define DRIFT_INDEXMAP_BUCKET_TAKEN 0x80u
#define DRIFT_INDEXMAP_MAX_PROBE 32u
#define DRIFT_INDEXMAP_MAX_INFO (DRIFT_INDEXMAP_BUCKET_TAKEN + DRIFT_INDEXMAP_MAX_PROBE)

#define DRIFT_INDEXMAP_MIN_CAPACITY 64u

#define SWAP(a, b) {__typeof(a) tmp; tmp = a; a = b; b = tmp;}

static inline uint DriftMapHash(DriftMap const* map, uintptr_t key){return key & (map->table.row_capacity - 1);}
static inline uint DriftMapNextIndex(DriftMap const* map, uint index){return (index + 1) & (map->table.row_capacity - 1);}

// Lookup the hash table index for a given key.
static uint DriftMapFindIndex(DriftMap const* map, uintptr_t key){
	uint index = DriftMapHash(map, key);
	u8 info = DRIFT_INDEXMAP_BUCKET_TAKEN;
	
	// Skip buckets that are empty or taken by an earlier index.
	for(; info < map->infobytes[index]; info++) index = DriftMapNextIndex(map, index);
	
	// Check all buckets with a matching index.
	for(; info == map->infobytes[index]; info++){
		if(key == map->keys[index]) return index;
		index = DriftMapNextIndex(map, index);
	}
	
	return DRIFT_INDEXMAP_NOT_FOUND;
}

static void DriftMapInitTable(DriftMap* map, DriftTableDesc desc){
	DriftTableInit(&map->table, desc);
	
	size_t capacity = map->table.row_capacity;
	memset(map->infobytes, 0x00, capacity*sizeof(*map->infobytes));
	memset(map->keys, 0x00, capacity*sizeof(*map->keys));
	memset(map->values, 0x00, capacity*sizeof(*map->values));
	
	DRIFT_ASSERT(DriftIsPOT(map->table.row_capacity), "DriftMap table not a power of two size.");
}

static void DriftMapResize(DriftMap* map){
	// Make a copy of the old map/table.
	DriftMap copy = *map;
	DriftTableDesc desc = map->table.desc;
	
	// Re allocate the table with double the capacity.
	desc.min_row_capacity = 2*copy.table.row_capacity;
	DriftMapInitTable(map, desc);
	
	// Reinsert then dispose of the old table.
	for(uint index = 0; index < copy.table.row_capacity; index++){
		if(copy.infobytes[index] >= DRIFT_INDEXMAP_BUCKET_TAKEN) DriftMapInsert(map, copy.keys[index], copy.values[index]);
	}
	
	// Dispose of the old table.
	DriftTableDestroy(&copy.table);
}

DriftMap* DriftMapInit(DriftMap* map, DriftMem* mem, const char* name, size_t min_capacity){
	*map = (DriftMap){0};
	
	size_t capacity = DRIFT_MAX(DRIFT_INDEXMAP_MIN_CAPACITY, DriftNextPOT(min_capacity));
	DriftMapInitTable(map, (DriftTableDesc){
			.name = name, .mem = mem, .min_row_capacity = capacity,
			.columns.arr = {
				DRIFT_DEFINE_COLUMN(map->infobytes),
				DRIFT_DEFINE_COLUMN(map->keys),
				DRIFT_DEFINE_COLUMN(map->values),
			},
		});
	
	return map;
}

void DriftMapDestroy(DriftMap* map){
	DriftTableDestroy(&map->table);
}

// Update the index for `key` with `value`.
// Returns the old index value (or the default value).
uintptr_t DriftMapInsert(DriftMap *map, uintptr_t key, uintptr_t value){
	// Hard coded load factor. Doesn't seem to be much reason to change it though.
	if(5*map->table.row_count > 4*map->table.row_capacity) DriftMapResize(map);
	
	uint index = DriftMapHash(map, key);
	for(u8 info = DRIFT_INDEXMAP_BUCKET_TAKEN; info < DRIFT_INDEXMAP_MAX_INFO; info++){
		if(map->infobytes[index] == info && map->keys[index] == key){
			// Update existing entry.
			SWAP(map->values[index], value);
			return value;
		} else if(map->infobytes[index] < info){
			SWAP(info, map->infobytes[index]);
			SWAP(key, map->keys[index]);
			SWAP(value, map->values[index]);
			
			if(info < DRIFT_INDEXMAP_BUCKET_TAKEN){
				// Bucket was empty.
				map->table.row_count++;
				return map->default_value;
			}
		}
		
		index = DriftMapNextIndex(map, index);
	}
	
	// DRIFT_INDEXMAP_MAX_INFO has been overflown.
	DriftMapResize(map);
	return DriftMapInsert(map, key, value);
}

// Lookup the index for a given key.
uintptr_t DriftMapFind(DriftMap const* map, uintptr_t key){
	uint index = DriftMapFindIndex(map, key);
	return (index == DRIFT_INDEXMAP_NOT_FOUND ? map->default_value : map->values[index]);
}

// Remove an index for a given key.
uintptr_t DriftMapRemove(DriftMap* map, uintptr_t key){
	uint index = DriftMapFindIndex(map, key);
	if(index == DRIFT_INDEXMAP_NOT_FOUND){
		return map->default_value;
	} else {
		uintptr_t prev_value = map->values[index];
		
		// Backwards shift.
		for(uint next = DriftMapNextIndex(map, index); map->infobytes[next] > DRIFT_INDEXMAP_BUCKET_TAKEN; index = next, next = DriftMapNextIndex(map, next)){
			map->infobytes[index] = map->infobytes[next] - 1;
			map->keys[index] = map->keys[next];
			map->values[index] = map->values[next];
		}
		
		// Mark the bucket as unused.
		map->infobytes[index] = 0;
		map->table.row_count -= 1;
		
		return prev_value;
	}
}

#if DRIFT_DEBUG
void unit_test_map(void){
	DriftMap map = {0};
	DriftMapInit(&map, DriftSystemMem, "test", 0);
	
	const uint seed = 2;
	const uint count = 256;
	
	// Insert values.
	srand(seed);
	for(uint i = 0; i < count; i++){
		uint r = rand();
		DRIFT_ASSERT(DriftMapFind(&map, r) == 0, "Test setup is bad. Repeated value in sequence.");
		
		DriftMapInsert(&map, r, i + 1);
		DRIFT_ASSERT(DriftMapFind(&map, r) == (i + 1), "Value does not match.");
	}
	DRIFT_ASSERT(map.table.row_count == count, "HashMap count is incorrect.");
	
	// Check probe lengths.
	uint histogram[DRIFT_INDEXMAP_MAX_INFO] = {};
	uint sum = 0;
	uint max_probe = 0;
	for(uint i = 0; i < map.table.row_count; i++){
		int probe = map.infobytes[i] - DRIFT_INDEXMAP_BUCKET_TAKEN;
		max_probe = (probe > (int)max_probe ? (uint)probe : max_probe);
		if(probe >= 0){
			sum += probe;
			histogram[probe] += 1;
		}
	}
	DRIFT_ASSERT(max_probe <= DRIFT_INDEXMAP_MAX_PROBE, "Maximum probe length exceeded.");
	
	// for(int i = 0; i < DRIFT_INDEXMAP_MAX_PROBE; i++) printf("histogram[%d]: % 6d\n", i, histogram[i]);
	// DRIFT_LOG("max_probe: %d, average: %f", max_probe, sum/map.table.row_count);
	// DRIFT_LOG("count: %d, capacity: %d", (int)map.table.row_count, (int)map.table.row_capacity);
	// DRIFT_LOG("load factor: %f", map.table.row_count/map.table.row_capacity);
	DRIFT_ASSERT(max_probe < DRIFT_INDEXMAP_MAX_INFO - DRIFT_INDEXMAP_BUCKET_TAKEN, "Max probe length exceeded.");
	
	// Find values
	srand(seed);
	for(uint i = 0; i < count; i++) DRIFT_ASSERT(DriftMapFind(&map, rand()) == (i + 1), "Incorrect value.");
	
	// Update values
	srand(seed);
	for(uint i = 0; i < count; i++) DRIFT_ASSERT(DriftMapInsert(&map, rand(), i + 2) == (i + 1), "Incorrect value.");
	DRIFT_ASSERT(map.table.row_count == count, "Incorrect count.");
	
	srand(seed);
	for(uint i = 0; i < count; i++) DRIFT_ASSERT(DriftMapFind(&map, rand()) == (i + 2), "Incorrect value.");
	
	// Remove some keys.
	srand(seed);
	for(uint i = 0; i < count; i++){
		uint k = rand();
		if(
			(i % 2 == 0) ||
			(i % 3 == 0) ||
			(i % 5 == 0)
		) DriftMapRemove(&map, k);
	}
	
	srand(seed);
	for(uint i = 0; i < count; i++){
		uint expected = i + 2;
		if(i % 2 == 0) expected = 0;
		if(i % 3 == 0) expected = 0;
		if(i % 5 == 0) expected = 0;
		DRIFT_ASSERT(DriftMapFind(&map, rand()) == expected, "Incorrect value.");
	}
	
	DriftMapDestroy(&map);
	DRIFT_LOG("IndexMap tests passed.");
}
#endif
