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

DriftMem* DriftLinearMemInit(void* buffer, size_t capacity, const char* label);


// MARK: Zone Memory Allocator.

typedef struct DriftZoneMemHeap DriftZoneMemHeap;
DriftZoneMemHeap* DriftZoneMemHeapNew(DriftMem* mem, const char* label);
// void DriftZoneMemHeapFree(DriftZoneMemHeap* heap);

DriftMem* DriftZoneMemAquire(DriftZoneMemHeap* heap, const char* label);
void DriftZoneMemRelease(DriftMem* zone);


// MARK: Buddy Block Memory allocator.
/*
typedef struct DriftBuddyMem DriftBuddyMem;
DriftBuddyMem* DriftBuddyMemInit(const char* label, void* buffer, size_t size);
void* DriftBuddyMemAlloc(DriftBuddyMem* mem, size_t size);
void* DriftBuddyMemRealloc(DriftBuddyMem* mem, void* ptr, size_t osize, size_t nsize);
void DriftBuddyMemDealloc(DriftBuddyMem* mem, void* ptr, size_t size);
*/

// MARK: Strings

char* DriftSMPrintf(DriftMem* mem, const char* format, ...);
char* DriftSMFormat(DriftMem* mem, const char* format, ...);

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
void* _DriftArrayGrow(void* ptr, size_t n_elt);
void _DriftArrayShiftUp(void* ptr, unsigned idx);

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

static inline size_t DriftArraySize(void* ptr){
	DriftArray* header = DriftArrayHeader(ptr);
	return header->count*header->elt_size;
}

static inline void* _DriftArrayEnsure(void* ptr, size_t n_elt){
	DriftArray* header = DriftArrayHeader(ptr);
	return header->count + n_elt > header->capacity ? _DriftArrayGrow(ptr, n_elt) : ptr;
}

#define DRIFT_ARRAY_PUSH(_ptr_, _elt_) (_ptr_ = _DriftArrayEnsure(_ptr_, 1), (_ptr_)[DriftArrayHeader(_ptr_)->count++] = _elt_)
#define DRIFT_ARRAY_POP(_ptr_) ({DriftArray* arr = DriftArrayHeader(_ptr_); arr->count ? _ptr_[--arr->count] : NULL;})
#define DRIFT_ARRAY_INSERT(_ptr_, _idx_, _elt_) (_ptr_ = _DriftArrayEnsure(_ptr_, 1), _DriftArrayShiftUp(_ptr_, _idx_), (ptr)[_idx_] = _elt_)
void DriftArrayTruncate(void* ptr, size_t len);

#define DRIFT_ARRAY_RANGE(_ptr_, _n_elt_) (_ptr_ = _DriftArrayEnsure(_ptr_, _n_elt_), (_ptr_) + DriftArrayHeader(_ptr_)->count)
void DriftArrayRangeCommit(void* ptr, void* cursor);

#define DRIFT_ARRAY_FOREACH(_arr_, _ptr_) \
	for(typeof(_arr_) _ptr_ = _arr_, _end_ = _arr_ + DriftArrayLength(_arr_); _ptr_ < _end_; _ptr_++)
