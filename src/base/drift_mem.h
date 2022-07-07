#pragma once

#define DRIFT_MEM_MIN_ALIGNMENT 16


// MARK: General memory functions.

typedef struct DriftMem DriftMem;

void* DriftAlloc(DriftMem* mem, size_t size);
void* DriftRealloc(DriftMem* mem, void* ptr, size_t osize, size_t nsize);
void DriftDealloc(DriftMem* mem, void* ptr, size_t size);

extern DriftMem* const DriftSystemMem;

#define DRIFT_COPY(_mem_, _expr_) ({typeof(_expr_)* ptr = DriftAlloc(_mem_, sizeof(_expr_)); *ptr = (_expr_); ptr;})


// MARK: Linear Memory Allocator.

typedef struct DriftLinearMem DriftLinearMem;
DriftLinearMem* DriftLinearMemInit(void* buffer, size_t capacity);
void* DriftLinearMemAlloc(DriftLinearMem* mem, size_t size);


// MARK: Zone Memory Allocator.

typedef struct DriftZoneMemHeap DriftZoneMemHeap;
DriftZoneMemHeap* DriftZoneMemHeapNew(const char* name, DriftMem* mem, size_t block_size, size_t max_blocks, size_t initial_blocks);
void DriftZoneMemHeapFree(DriftZoneMemHeap* heap);

typedef struct DriftZoneMem DriftZoneMem;
DriftZoneMem* DriftZoneMemAquire(DriftZoneMemHeap* heap, const char* name);
void DriftZoneMemRelease(DriftZoneMem* zone);
void* DriftZoneMemAlloc(DriftZoneMem* zone, size_t size);

DriftMem* DriftZoneMemWrap(DriftZoneMem* zone);


// MARK: Buddy Block Memory allocator.

typedef struct DriftBuddyMem DriftBuddyMem;
DriftBuddyMem* DriftBuddyMemInit(const char* name, void* buffer, size_t size);
void* DriftBuddyMemAlloc(DriftBuddyMem* mem, size_t size);
void* DriftBuddyMemRealloc(DriftBuddyMem* mem, void* ptr, size_t osize, size_t nsize);
void DriftBuddyMemDealloc(DriftBuddyMem* mem, void* ptr, size_t size);


// MARK: Strings

const char* DriftSMPrintf(DriftMem* mem, const char* format, ...);
const char* DriftSMFormat(DriftMem* mem, const char* format, ...);

// MARK: Stretchy Buffers.

#define DRIFT_STRETCHY_BUFER_MIN_CAPACITY 4

typedef struct {
// #if DRIFT_DEBUG
	u64 _magic;
// #endif
	DriftMem* mem;
	size_t count, capacity, elt_size;
	
	u8 array[];
} DriftArray;

#define DRIFT_ARRAY(type) type*

void* _DriftArrayNew(DriftMem* mem, size_t capacity, size_t elt_size);
#define DRIFT_ARRAY_NEW(mem, capacity, type) (type*)_DriftArrayNew(mem, capacity, sizeof(type))

void DriftArrayFree(void* ptr);

static inline DriftArray* DriftArrayHeader(void* ptr){
	DriftArray* header = ptr - sizeof(DriftArray);
	DRIFT_ASSERT(header->_magic == 0xABCDABCDABCDABCD, "DriftArray header is corrupt.");
	return header;
}

static inline size_t DriftArrayLength(void* ptr){
	return DriftArrayHeader(ptr)->count;
}

void* _DriftArrayGrow(void* ptr, size_t n_elt);
static inline void* _DriftArrayEnsure(void* ptr, size_t n_elt){
	DriftArray* header = DriftArrayHeader(ptr);
	return header->count + n_elt > header->capacity ? _DriftArrayGrow(ptr, n_elt) : ptr;
}

#define DRIFT_ARRAY_PUSH(ptr, elt) (ptr = _DriftArrayEnsure(ptr, 1), (ptr)[DriftArrayHeader(ptr)->count++] = elt)

#define DRIFT_ARRAY_RANGE(ptr, n_elt) ((ptr) = _DriftArrayEnsure(ptr, n_elt), (ptr) + DriftArrayHeader(ptr)->count)
void DriftArrayRangeCommit(void* ptr, void* cursor);

#define DRIFT_ARRAY_FOREACH(_arr_, _ptr_) \
	for(typeof(_arr_) _ptr_ = _arr_, _end_ = _arr_ + DriftArrayLength(_arr_); _ptr_ < _end_; _ptr_++)
