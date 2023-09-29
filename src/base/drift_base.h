/*
This file is part of Veridian Expanse.

Veridian Expanse is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

Veridian Expanse is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with Veridian Expanse. If not, see <https://www.gnu.org/licenses/>.
*/

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

#include "tina/tina.h"
#include "tina/tina_jobs.h"

typedef unsigned uint;
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;

typedef struct {
	uint idx0, idx1;
} DriftIndexPair;

#include "base/drift_math.h"

typedef struct DriftMem DriftMem;
typedef struct DriftTable DriftTable;

// Utils

extern uint DRIFT_SRC_PREFIX_LENGTH;
void DriftUtilInit(void);

size_t DriftSNFormat(char* buffer, size_t size, const char* format, ...);
size_t DriftVSNFormat(char* buffer, size_t size, const char* format, va_list* args);

void _DriftLogf(const char *format, const char *file, uint line, const char *message, ...);
void _DriftLog(const char *format, const char *file, uint line, const char *message, ...);
void DriftBreakpoint(void);
_Noreturn void DriftAbort(void);

typedef enum {DRIFT_ASSERT_WARN, DRIFT_ASSERT_ERROR, DRIFT_ASSERT_HARD} DriftAssertType;
void _DriftAssertHelper(const char *condition, const char *file, uint line, DriftAssertType type, const char *message, ...);

#define DRIFT_VAR(_var_, _expr_) typeof(_expr_) _var_ = (_expr_)
#define DRIFT_STR(_S_) #_S_
#define DRIFT_XSTR(_S) DRIFT_STR(_S)

#define DRIFT_NAME_SIZE 32
typedef struct {char str[DRIFT_NAME_SIZE];} DriftName;
void DriftNameCopy(DriftName* dst, const char* src);

#ifndef NDEBUG
	#define DRIFT_DEBUG 1
	#define DRIFT_BREAKPOINT(_cond_) if(!(_cond_)) DriftBreakpoint();
	#define DRIFT_LOG_DEBUG(...) _DriftLog("[DEBUG] %s:%d: %s\n", __FILE__, __LINE__, __VA_ARGS__)
	#define DRIFT_ASSERT(_condition_, ...) if(!(_condition_)){_DriftAssertHelper(#_condition_, __FILE__, __LINE__, DRIFT_ASSERT_ERROR, __VA_ARGS__); DriftAbort();}
	#define DRIFT_ASSERT_WARN(_condition_, ...) if(!(_condition_)) _DriftAssertHelper(#_condition_, __FILE__, __LINE__, DRIFT_ASSERT_WARN, __VA_ARGS__)
	#define DRIFT_UNREACHABLE() DRIFT_ASSERT(false, "This should be unreachable.");
#else
	#define DRIFT_DEBUG 0
	#define DRIFT_BREAKPOINT(cond){}
	#define DRIFT_LOG_DEBUG(...){}
	#define	DRIFT_ASSERT(_condition_, ...){}
	#define	DRIFT_ASSERT_WARN(_condition_, ...){}
	#define DRIFT_UNREACHABLE() __builtin_unreachable()
#endif

#define DRIFT_LOG(...) _DriftLog("[LOG] %s:%d: %s\n", __FILE__, __LINE__, __VA_ARGS__)
#define DRIFT_LOGF(...) _DriftLogf("[LOG] %s:%d: %s\n", __FILE__, __LINE__, __VA_ARGS__)
#define DRIFT_ASSERT_HARD(_condition_, ...) if(!(_condition_)){_DriftAssertHelper(#_condition_, __FILE__, __LINE__, DRIFT_ASSERT_HARD, __VA_ARGS__); DriftAbort();}
#define DRIFT_ABORT(...) {_DriftLog("[Abort] %s:%d\n\tReason: %s\n", __FILE__, __LINE__, __VA_ARGS__); DriftAbort();}
#define DRIFT_NYI() {_DriftLog("[Abort] %s:%d\n\tReason: Not yet implemented.\n", __FILE__, __LINE__, ""); DriftAbort();}

u64 DriftTimeNanos(void);

typedef struct {
	void* ptr;
	size_t size;
} DriftData;

typedef struct DriftIO DriftIO;
typedef void DriftIOFunc(DriftIO* io);

typedef struct DriftIO {
	void* user_ptr;
	bool read;
	
	DriftIOFunc* _io_func;
	tina* _coro;
} DriftIO;

void DriftIOBlock(DriftIO* io, const char* label, void* ptr, size_t size);

bool DriftIOFileRead(const char* filename, DriftIOFunc* io_func, void* user_ptr);
void DriftIOFileWrite(const char* filename, DriftIOFunc* io_func, void* user_ptr);
size_t DriftIOSize(DriftIOFunc* io_func, void* user_ptr);

void DriftAssetsReset(void);
DriftData DriftAssetLoad(DriftMem* mem, const char* filename);
DriftData DriftAssetLoadf(DriftMem* mem, const char* format, ...);

typedef struct {uint w, h; void* pixels;} DriftImage;
DriftImage DriftAssetLoadImage(DriftMem* mem, const char* filename);
void DriftImageFree(DriftMem* mem, DriftImage img);

uint DriftLog2Ceil(u64 n);
u64 DriftNextPOT(u64 n);
static inline bool DriftIsPOT(uint n){return n == DriftNextPOT(n);}

#define DRIFT_MIN(a, b) ({typeof(a) _a = a; typeof(b) _b = b; _a < _b ? _a : _b;})
#define DRIFT_MAX(a, b) ({typeof(a) _a = a; typeof(b) _b = b; _a > _b ? _a : _b;})

// Memory

#define DRIFT_MEM_MIN_ALIGNMENT 16
typedef struct DriftMem DriftMem;
void* DriftAlloc(DriftMem* mem, size_t size);
void* DriftRealloc(DriftMem* mem, void* ptr, size_t osize, size_t nsize);
void DriftDealloc(DriftMem* mem, void* ptr, size_t size);
#define DRIFT_COPY(_mem_, _expr_) ({typeof(_expr_)* ptr = DriftAlloc(_mem_, sizeof(_expr_)); *ptr = (_expr_); ptr;})

extern DriftMem* const DriftSystemMem;

DriftMem* DriftLinearMemMake(void* buffer, size_t capacity, const char* label);

DriftMem* DriftListMemNew(DriftMem* parent_mem, const char* label);
void DriftListMemFree(DriftMem* mem);

typedef struct DriftZoneMemHeap DriftZoneMemHeap;
DriftZoneMemHeap* DriftZoneMemHeapNew(DriftMem* mem, const char* label);
void DriftZoneMemHeapFree(DriftZoneMemHeap* heap);

typedef struct {
	uint blocks_allocated, blocks_used;
	uint zones_allocated, zones_used;
	
	const char* zone_names[64];
} DriftZoneHeapInfo;

DriftZoneHeapInfo DriftZoneHeapGetInfo(DriftZoneMemHeap* heap);

DriftMem* DriftZoneMemAquire(DriftZoneMemHeap* heap, const char* label);
void DriftZoneMemRelease(DriftMem* zone);

char* DriftSMPrintf(DriftMem* mem, const char* format, ...);
char* DriftSMFormat(DriftMem* mem, const char* format, ...);

#define DRIFT_STRETCHY_BUFER_MIN_CAPACITY 4
typedef struct {
	DriftMem* mem;
	size_t count, capacity, elt_size;
	
	u64 _magic;
	u8 array[];
} DriftArray;
#define DRIFT_ARRAY(type) type*

void* _DriftArrayNew(DriftMem* mem, size_t capacity, size_t elt_size);
void* _DriftArrayGrow(void* ptr, size_t n_elt);
void _DriftArrayShiftUp(void* ptr, unsigned idx);

#define DRIFT_ARRAY_NEW(mem, capacity, type) (type*)_DriftArrayNew(mem, capacity, sizeof(type))
void DriftArrayFree(void* ptr);

static inline DriftArray* DriftArrayHeader(void* ptr){
	DriftArray* header = ptr - sizeof(DriftArray);
	DRIFT_ASSERT(header->_magic == 0xABCDABCDABCDABCD, "DriftArray header is corrupt.");
	return header;
}

static inline size_t DriftArrayLength(void* ptr){return DriftArrayHeader(ptr)->count;}
static inline size_t DriftArraySize(void* ptr){DriftArray* h = DriftArrayHeader(ptr); return h->count*h->elt_size;}

static inline void* _DriftArrayEnsure(void* ptr, size_t n_elt){
	DriftArray* header = DriftArrayHeader(ptr);
	return header->count + n_elt > header->capacity ? _DriftArrayGrow(ptr, n_elt) : ptr;
}

#define DRIFT_ARRAY_PUSH(_ptr_, _elt_) (_ptr_ = _DriftArrayEnsure(_ptr_, 1), (_ptr_)[DriftArrayHeader(_ptr_)->count++] = _elt_)
#define DRIFT_ARRAY_POP(_ptr_, _default_) ({DriftArray* _arr_ = DriftArrayHeader(_ptr_); _arr_->count ? (_ptr_)[--_arr_->count] : _default_;})
void DriftArrayTruncate(void* ptr, size_t len);

#define DRIFT_ARRAY_RANGE(_ptr_, _n_elt_) (_ptr_ = _DriftArrayEnsure(_ptr_, _n_elt_), (_ptr_) + DriftArrayHeader(_ptr_)->count)
void DriftArrayRangeCommit(void* ptr, void* cursor);

#define DRIFT_ARRAY_FOREACH(_arr_, _ptr_) for(typeof(_arr_) _ptr_ = _arr_, _end_ = _arr_ + DriftArrayLength(_arr_); _ptr_ < _end_; _ptr_++)

// Threading

enum {
	DRIFT_JOB_QUEUE_WORK,
	DRIFT_JOB_QUEUE_GFX,
	DRIFT_JOB_QUEUE_MAIN,
	_DRIFT_JOB_QUEUE_COUNT,
};

enum {
	DRIFT_THREAD_ID_MAIN,
	DRIFT_THREAD_ID_GFX,
	DRIFT_THREAD_ID_WORKER0,
};

uint DriftGetThreadID(void);
void DriftAssertMainThread(void);
void DriftAssertGfxThread(void);

// Tables

#pragma once

typedef struct {
	char const* name;
	size_t size;
	void* ptr;
	void** user;
} DriftColumn;

#define DRIFT_TABLE_MAX_COLUMNS 16lu
#define DRIFT_TABLE_MIN_ALIGNMENT 64llu
#define DRIFT_TABLE_MIN_ROW_CAPACITY 64llu
#define DRIFT_TABLE_GROWTH_FACTOR 2

typedef struct {DriftColumn arr[DRIFT_TABLE_MAX_COLUMNS];} DriftColumnSet;

typedef struct {
	char const* name;
	DriftMem* mem;
	size_t min_row_capacity;
	DriftColumnSet columns;
} DriftTableDesc;

struct DriftTable {
	DriftTableDesc desc;
	size_t row_capacity, row_count, row_size;
	
	DriftName _names[1 + DRIFT_TABLE_MAX_COLUMNS];
	
	// Internal pointer to the table's memory.
	void* buffer;
};

#define DRIFT_DEFINE_COLUMN(ptr_expr) (DriftColumn){ \
	.name = DRIFT_STR(ptr_expr), \
	.size = sizeof(*ptr_expr), \
	.user = (void**)&ptr_expr, \
}

DriftTable* DriftTableInit(DriftTable* table, DriftTableDesc desc);
void DriftTableDestroy(DriftTable* table);
void DriftTableIO(DriftTable* table, DriftIO* io);

void DriftTableResize(DriftTable* table, size_t row_capacity);

static inline void DriftTableEnsureCapacity(DriftTable *table, size_t row_capacity){
	if(row_capacity > table->row_capacity) DriftTableResize(table, row_capacity*DRIFT_TABLE_GROWTH_FACTOR);
}

static inline uint DriftTablePushRow(DriftTable* table){
	DriftTableEnsureCapacity(table, table->row_count + 1);
	return table->row_count++;
}

void DriftTableClearRow(DriftTable* table, uint idx);
void DriftTableCopyRow(DriftTable* table, uint dst_idx, uint src_idx);

// Maps

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

uintptr_t DriftFNV64Str(const char* str);
uintptr_t DriftFNV64(const u8* ptr, size_t size);

// Entities

// http://bitsquid.blogspot.com/2014/08/building-data-oriented-entity-system.html
#define DRIFT_ENTITY_INDEX_BITS 16u
#define DRIFT_ENTITY_GENERATION_BITS 8u
#define DRIFT_ENTITY_TAG_BITS 4u

#define DRIFT_ENTITY_TAG_STATIC 0x1000000

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

DriftEntity DriftEntitySetAquire(DriftEntitySet* set, uint tag);
void DriftEntitySetRetire(DriftEntitySet* set, DriftEntity entity);

static inline bool DriftEntitySetCheck(DriftEntitySet *set, DriftEntity entity){
	uint idx = DriftEntityIndex(entity);
	return idx != 0 && set->generations[idx] == DriftEntityGeneration(entity);
}

// Components

typedef struct DriftComponent {
	DriftTable table;
	DriftMap map;
	bool reset_on_hotload;
	
	uint count;
	uint gc_cursor;
} DriftComponent;

DriftComponent* DriftComponentInit(DriftComponent* component, DriftTableDesc desc);
void DriftComponentDestroy(DriftComponent* component);

void DriftComponentIO(DriftComponent* component, DriftIO* io);

// Add a component for the entity and return it's index.
uint DriftComponentAdd(DriftComponent *component, DriftEntity entity);
uint DriftComponentAdd2(DriftComponent *component, DriftEntity entity, bool unique);

// Delete a component for the given entity.
void DriftComponentRemove(DriftComponent* component, DriftEntity entity);

// Find the component index for a given entity.
static inline uint DriftComponentFind(DriftComponent* component, DriftEntity entity){
	return DriftMapFind(&component->map, entity.id);
}

static inline DriftEntity* DriftComponentGetEntities(DriftComponent* component){
	return component->table.desc.columns.arr[0].ptr;
}

// Clean up components for deleted entities.
// Higher values for 'pressure' cause more cleanup.
void DriftComponentGC(DriftComponent* component, DriftEntitySet* entities, uint pressure);

#define DRIFT_COMPONENT_FOREACH(_comp_, _idx_) \
	for(uint _idx_ = 1, count = (_comp_)->count; _idx_ <= count; _idx_++)

typedef struct {
	uint* variable;
	DriftComponent* component;
	bool optional;
} DriftComponentJoin;

#define DRIFT_JOIN_MAX_COMPONENTS 8
typedef struct {
	DriftEntity entity;
	DriftComponentJoin joins[DRIFT_JOIN_MAX_COMPONENTS];
	uint count;
} DriftJoin;

DriftJoin DriftJoinMake(DriftComponentJoin* joins);
bool DriftJoinNext(DriftJoin* join);

// R-trees

#define DRIFT_RTREE_BRANCH_FACTOR 24
typedef struct {
	u8 count;
	// loose bound for leaves, regulal bound for internal nodes.
	DriftAABB2 bb0[DRIFT_RTREE_BRANCH_FACTOR];
	// object bound for leaves, unused for internal nodes (maybe use for 8-dop in the future?)
	DriftAABB2 bb1[DRIFT_RTREE_BRANCH_FACTOR];
	uint child[DRIFT_RTREE_BRANCH_FACTOR];
} DriftRNode;

typedef struct {
	DriftTable t;
	uint root, count, leaf_depth;
	uint pool_idx;
	
	DriftRNode* node;
	uint* pool_arr;
} DriftRTree;

typedef void DriftRTreeBoundFunc(uint* indexes, DriftAABB2* bounds, uint count, void* user_data);
void DriftRTreeUpdate(DriftRTree* tree, uint obj_count, DriftRTreeBoundFunc bound_func, void* bound_data, tina_job* job, DriftMem* mem);
DRIFT_ARRAY(DriftIndexPair) DriftRTreePairs(DriftRTree* tree, tina_job* job, DriftMem* mem);

// Audio

typedef enum {
	DRIFT_BUS_UI,
	DRIFT_BUS_HUD,
	DRIFT_BUS_SFX,
	_DRIFT_BUS_COUNT,
} DriftAudioBusID;

typedef struct DriftAudioContext DriftAudioContext;
DriftAudioContext* DriftAudioContextNew(tina_scheduler* sched);
void DriftAudioContextFree(DriftAudioContext* ctx);

void DriftAudioSetParams(float master_volume, float music_volume, float effects_volume);
void DriftAudioBusSetActive(DriftAudioBusID bus, bool active);

#define DRIFT_IMPULSE_RESPONSE_LEN (64*1024)
void DriftAudioSetReverb(float dry, float wet, float decay, float lowpass);

void DriftAudioStartMusic(void);
void DriftAudioPause(bool state);

typedef struct {u32 id;} DriftAudioSourceID;
bool DriftAudioSourceActive(DriftAudioSourceID source_id);

void DriftAudioLoadSamples(tina_job* job, const char* names[], uint count);

typedef struct {
	float gain, pan, pitch;
	bool loop;
} DriftAudioParams;

typedef uint DriftSFX;
typedef struct {DriftAudioSourceID source;} DriftAudioSampler;
DriftAudioSampler DriftAudioPlaySample(DriftAudioBusID bus, DriftSFX sfx, DriftAudioParams params);
void DriftAudioSamplerSetParams(DriftAudioSampler sampler, DriftAudioParams params);

void DriftImAudioSet(DriftAudioBusID bus, uint sfx, DriftAudioSampler* sampler, DriftAudioParams params);
void DriftImAudioUpdate(void);


// Unit tests

#if DRIFT_DEBUG
void unit_test_util(void);
void unit_test_math(void);
void unit_test_entity(void);
void unit_test_map(void);
void unit_test_component(void);
void unit_test_rtree(void);
#endif

#include "base/drift_gfx.h"
#include "base/drift_app.h"
