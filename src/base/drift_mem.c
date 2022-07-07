#include <stdalign.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "tinycthread/tinycthread.h"

#include "drift_types.h"
#include "drift_util.h"
#include "drift_mem.h"

uint DriftAppGetThreadID(void);


typedef void* DriftMemFunc(void* ctx, void* ptr, size_t osize, size_t nsize);

struct DriftMem {
	char const* name;
	void* ctx;
	DriftMemFunc* func;
};

void* DriftAlloc(DriftMem* mem, size_t size){return mem->func(mem->ctx, NULL, 0, size);}
void* DriftRealloc(DriftMem* mem, void* ptr, size_t osize, size_t nsize){
	if(ptr == NULL || osize == 0) DRIFT_ASSERT_WARN(ptr == NULL && osize == 0, "Invalid realloc params.");
	// DRIFT_LOG_DEBUG("Realloc from %u to %u bytes.", osize, nsize);
	return mem->func(mem->ctx, ptr, osize, nsize);
}
void DriftDealloc(DriftMem* mem, void* ptr, size_t size){if(ptr) mem->func(mem->ctx, ptr, size, 0);}

static void* DriftSystemMemFunc(void* ctx, void* ptr, size_t osize, size_t nsize){
	if(osize == 0){
		// return calloc(1, nsize);
		DRIFT_ASSERT(nsize < 1024*1024*1024, "Huge allocation detected!");
		void* ptr = calloc(1, nsize);
#if DRIFT_DEBUG
		// memset(ptr, 0xFF, nsize);
#endif
		return ptr;
	} else if(nsize == 0){
		free(ptr);
		return NULL;
	} else {
		return realloc(ptr, nsize);
	}
}

DriftMem* const DriftSystemMem = &(DriftMem){
	.name = "DriftSystemMemory",
	.func = DriftSystemMemFunc,
};


// MARK: Linear Allocator.

struct DriftLinearMem {
	uintptr_t buffer, cursor;
};

void* DriftLinearMemAlloc(DriftLinearMem* mem, size_t size){
	// Check for underflow first.
	if(size <= mem->cursor){
		// Bump downwards and apply alignment.
		uintptr_t cursor = (mem->cursor - size) & -DRIFT_MEM_MIN_ALIGNMENT;
		// Check remaining space.
		if(cursor >= mem->buffer){
			mem->cursor = cursor;
			return (void*)cursor;
		}
	}
	
	return NULL;
}

DriftLinearMem* DriftLinearMemInit(void* buffer, size_t size){
	// Allocate the header from the block, copy, and return it.
	DriftLinearMem tmp = {.buffer = (uintptr_t)buffer, .cursor = (uintptr_t)buffer + size};
	DriftLinearMem* mem = DriftLinearMemAlloc(&tmp, sizeof(tmp));
	(*mem) = tmp;
	return mem;
}


// MARK: Zone Allocator.

static void* DriftZoneMemInternalMalloc(DriftZoneMemHeap* heap, size_t size);

typedef struct DriftZoneMemNode {
	struct DriftZoneMemNode* next;
	void* ptr;
	size_t size;
} DriftZoneMemNode;

static void* DriftZoneMemNodeNew(DriftZoneMemHeap* heap, void* ptr, size_t size){
	DriftZoneMemNode* node = DriftZoneMemInternalMalloc(heap, sizeof(*node));
	(*node) = (DriftZoneMemNode){.ptr = ptr, .size = size};
	return node;
}

static void DriftZoneMemListPush(DriftZoneMemNode** list, DriftZoneMemNode* node){
	node->next = (*list);
	(*list) = node;
}

static DriftZoneMemNode* DriftZoneMemListPop(DriftZoneMemNode** list){
	DriftZoneMemNode* node = (*list);
	if(node) (*list) = node->next;
	return node;
}

struct DriftZoneMemHeap {
	const char* name;
	DriftMem* parent_mem;
	size_t block_size, max_blocks;
	
	mtx_t lock;
	DriftLinearMem* internal_mem;
	size_t block_count;
	DriftZoneMemNode* blocks;
	DriftZoneMemNode* pooled_blocks;
	DriftZoneMemNode* pooled_zones;
};

#define MAX_THREADS 64

struct DriftZoneMem {
	DriftZoneMemNode node;
	DriftZoneMemHeap* parent_heap;
	const char* name;
	
	DriftZoneMemNode* blocks;
	DriftLinearMem* current_allocator[MAX_THREADS];
};

#define DRIFT_ZONE_MEM_INTERNAL_BLOCK_SIZE (64*1024)

static void* DriftZoneMemInternalMalloc(DriftZoneMemHeap* heap, size_t size){
	if(heap->internal_mem){
		void* ptr = DriftLinearMemAlloc(heap->internal_mem, size);
		if(ptr) return ptr;
	}
	
	// Allocate new internal block.
	if(heap->blocks) DRIFT_LOG("Zone heap '%s' allocating new internal block.", heap->name);
	void* block = DriftAlloc(heap->parent_mem, DRIFT_ZONE_MEM_INTERNAL_BLOCK_SIZE);
	heap->internal_mem = DriftLinearMemInit(block, DRIFT_ZONE_MEM_INTERNAL_BLOCK_SIZE);
	DriftZoneMemListPush(&heap->blocks, DriftZoneMemNodeNew(heap, block, DRIFT_ZONE_MEM_INTERNAL_BLOCK_SIZE));
	return DriftLinearMemAlloc(heap->internal_mem, size);
}

DriftZoneMemNode* DriftZoneMemHeapAllocateBlock(DriftZoneMemHeap* heap){
	DRIFT_ASSERT_HARD(heap->block_count < heap->max_blocks, "Zone heap '%s' is full!", heap->name);
	DriftZoneMemNode* block_node = DriftZoneMemNodeNew(heap, DriftAlloc(heap->parent_mem, heap->block_size), heap->block_size);
	DriftZoneMemListPush(&heap->blocks, block_node);
	
	heap->block_count++;
	return block_node;
}

DriftZoneMemHeap* DriftZoneMemHeapNew(const char* name, DriftMem* mem, size_t block_size, size_t max_blocks, size_t initial_blocks){
	DRIFT_ASSERT_WARN(block_size >= (1 << 20), "Zone heap (%s) block size is kinda small? (%d bytes)", name, block_size);
	
	DriftZoneMemHeap tmp = (DriftZoneMemHeap){.name = name, .parent_mem = mem, .block_size = block_size, .max_blocks = max_blocks};
	DriftZoneMemHeap* heap = DriftZoneMemInternalMalloc(&tmp, sizeof(*heap));
	(*heap) = tmp;
	
	// Allocate initial blocks for the pool.
	for(uint i = 0; i < initial_blocks; i++){
		DriftZoneMemListPush(&heap->pooled_blocks, DriftZoneMemHeapAllocateBlock(heap));
	}
	
	mtx_init(&heap->lock, mtx_plain);
	return heap;
}

void DriftZoneHeapFree(DriftZoneMemHeap* heap){
	DRIFT_NYI(); // TODO This has never actually been tested?
	mtx_destroy(&heap->lock);
	
	// Free blocks in reverse order.
	DriftZoneMemNode* cursor = heap->blocks;
	while(cursor){
		DriftZoneMemNode* node = cursor;
		cursor = cursor->next;
		DriftDealloc(heap->parent_mem, node->ptr, node->size);
	}
}

DriftZoneMem* DriftZoneMemAquire(DriftZoneMemHeap* heap, const char* name){
	mtx_lock(&heap->lock);
	DriftZoneMemNode* zone_node = DriftZoneMemListPop(&heap->pooled_zones);
	// Make a new zone if there isn't a pooled one.
	DriftZoneMem* zone = zone_node ? zone_node->ptr : DriftZoneMemInternalMalloc(heap, sizeof(*zone));
	mtx_unlock(&heap->lock);
	
	(*zone) = (DriftZoneMem){.node.ptr = zone, .parent_heap = heap, .name = name};
	return zone;
}

void DriftZoneMemRelease(DriftZoneMem* zone){
	DriftZoneMemHeap* heap = zone->parent_heap;
	mtx_lock(&heap->lock);
	
	DriftZoneMemNode* cursor = zone->blocks;
	while(cursor){
		DriftZoneMemNode* node = cursor;
		cursor = cursor->next;
		DriftZoneMemListPush(&heap->pooled_blocks, node);
	}
	
	DriftZoneMemListPush(&heap->pooled_zones, &zone->node);
	mtx_unlock(&heap->lock);
}

void* DriftZoneMemAlloc(DriftZoneMem* zone, size_t size){
	uint thread_id = DriftAppGetThreadID();
	DRIFT_ASSERT_HARD(thread_id < MAX_THREADS, "DriftZoneMalloc(): Invalid thread id.");
	
	// Fast path when there is an allocator with enough space.
	if(zone->current_allocator[thread_id]){
		void* ptr = DriftLinearMemAlloc(zone->current_allocator[thread_id], size);
		if(ptr) return ptr;
	}
	
	DriftZoneMemHeap* heap = zone->parent_heap;
	DRIFT_ASSERT(size < heap->block_size, "Allocation size exceeds block size.");
	
	// Get a new block.
	mtx_lock(&heap->lock);
	DriftZoneMemNode* block_node = DriftZoneMemListPop(&heap->pooled_blocks);
	if(block_node == NULL){
		// No free blocks. Allocate a new one and push it onto the block list.
		block_node = DriftZoneMemHeapAllocateBlock(heap);
		DRIFT_LOG("Zone heap '%s' allocated new block. (%d/%d)", heap->name, heap->block_count, heap->max_blocks);
	}
	DRIFT_ASSERT_HARD(block_node->ptr, "Zone heap '%s' failed to allocate block!", heap->name);
	
	// Claim the block.
	DriftZoneMemListPush(&zone->blocks, block_node);
	mtx_unlock(&heap->lock);
	
	// Initialize the allocator.
	zone->current_allocator[thread_id] = DriftLinearMemInit(block_node->ptr, zone->parent_heap->block_size);
	void* ptr = DriftLinearMemAlloc(zone->current_allocator[thread_id], size);
	DRIFT_ASSERT(ptr, "Failed to allocate zone memory!");
	return ptr;
}

typedef struct {
	DriftZoneMem* zone;
	DriftMem mem;
} DriftZoneMemContext;

static void* DriftZoneMemFunc(void* ctx, void* ptr, size_t osize, size_t nsize){
	if(nsize <= osize){
		// If shrinking or dellocating, do nothing.
		return (nsize > 0 ? ptr : NULL);
	} else {
		// Combined alloc/grow.
		DriftZoneMemContext* zctx = ctx;
		void* new_ptr = DriftZoneMemAlloc(zctx->zone, nsize);
		if(osize > 0) memcpy(new_ptr, ptr, osize);
		return new_ptr;
	}
}

DriftMem* DriftZoneMemWrap(DriftZoneMem* zone){
	DriftZoneMemContext* ctx = DriftZoneMemAlloc(zone, sizeof(*ctx));
	(*ctx) = (DriftZoneMemContext){.zone = zone, .mem = {.name = zone->name, .func = DriftZoneMemFunc, .ctx = ctx}};
	return &ctx->mem;
}


// MARK: Buddy Allocator.

typedef struct DriftBuddyMemNode {
	struct DriftBuddyMemNode* prev;
	struct DriftBuddyMemNode* next;
} DriftBuddyMemNode;

#define DRIFT_MEMORY_BLOCK_MAX_LEVELS 32
#define DRIFT_MEMORY_BLOCK_MIN_ALLOC_SIZE sizeof(DriftBuddyMemNode)

struct DriftBuddyMem {
	void* buffer;
	size_t capacity, available;
	
	int log2_min, log2_max;
	DriftBuddyMemNode* free_list[DRIFT_MEMORY_BLOCK_MAX_LEVELS];
	uint8_t free_tree[];
};

static void DriftBuddyMemListPush(DriftBuddyMemNode** list, DriftBuddyMemNode* node){
	(*node) = (DriftBuddyMemNode){.next = *list};
	if(*list) (*list)->prev = node;
	(*list) = node;
}

static void* DriftBuddyMemListRemove(DriftBuddyMemNode** list, DriftBuddyMemNode* node){
	if(node->prev) node->prev->next = node->next; else (*list) = node->next;
	if(node->next) node->next->prev = node->prev;
	return node;
}

static bool DriftBuddyMemIsAllocated(DriftBuddyMem* mem, int idx){return (mem->free_tree[idx/8] & (1 << (idx & 7))) != 0;}
static void DriftBuddyMemFlipAllocated(DriftBuddyMem* mem, int idx){mem->free_tree[idx/8] ^= (1 << (idx & 7));}
static uintptr_t DriftBuddyMemOffset(DriftBuddyMem* mem, void* ptr){return ptr - mem->buffer;}
static size_t DriftBuddyMemSizeForLevel(DriftBuddyMem* mem, int level){return mem->capacity >> level;}
static int DriftBuddyMemMaxLevel(DriftBuddyMem* mem){return mem->log2_max - mem->log2_min;}

static int DriftBuddyMemLevelForSize(DriftBuddyMem* mem, size_t size){
	int log2 = DriftLog2Ceil(size);
	return mem->log2_max - (log2 > mem->log2_min ? log2 : mem->log2_min);
}

static int DriftBuddyMemPtrToIndex(DriftBuddyMem* mem, void* ptr, int level){
	return (1 << level) + (DriftBuddyMemOffset(mem, ptr)/DriftBuddyMemSizeForLevel(mem, level));
}

static void* DriftBuddyMemBuddyPtr(DriftBuddyMem* mem, void* ptr, int level){
	return mem->buffer + (DriftBuddyMemOffset(mem, ptr) ^ DriftBuddyMemSizeForLevel(mem, level));
}

static DriftBuddyMemNode* DriftBuddyMemAllocBlock(DriftBuddyMem* mem, int level){
	if(level < 0) return NULL;

	DriftBuddyMemNode** list = &mem->free_list[level];
	DriftBuddyMemNode* block = (*list);
	if(block){
		DriftBuddyMemListRemove(list, block);
		mem->available -= DriftBuddyMemSizeForLevel(mem, level);
	} else if((block = DriftBuddyMemAllocBlock(mem, level - 1))){
		// Split a super block, push the buddy onto the free list.
		DriftBuddyMemListPush(list, DriftBuddyMemBuddyPtr(mem, block, level));
		mem->available += DriftBuddyMemSizeForLevel(mem, level);
	} else {
		// No super block available.
		return NULL;
	}

	DriftBuddyMemFlipAllocated(mem, DriftBuddyMemPtrToIndex(mem, block, level));
	return block;
}

static void DriftBuddyMemDeallocBlock(DriftBuddyMem* mem, void* ptr, int level, int idx){
	DriftBuddyMemFlipAllocated(mem, idx);

	if(DriftBuddyMemIsAllocated(mem, idx ^ 1)){
		// Buddy not free. Push block onto the free list.
		DriftBuddyMemListPush(&mem->free_list[level], ptr);
		mem->available += DriftBuddyMemSizeForLevel(mem, level);
	} else {
		// Buddy is free, remove it from free list.
		DriftBuddyMemListRemove(&mem->free_list[level], DriftBuddyMemBuddyPtr(mem, ptr, level));
		mem->available -= DriftBuddyMemSizeForLevel(mem, level);

		// Free the superblock.
		void* super_ptr = mem->buffer + (DriftBuddyMemOffset(mem, ptr) & ~(mem->capacity >> level));
		DriftBuddyMemDeallocBlock(mem, super_ptr, level - 1, idx/2);
	}
}

static DriftBuddyMem* DriftBuddyMemInitSeparate(const char* name, void* header_buffer, size_t header_size, void* memory_buffer, size_t memory_size){
	// Assert: header size, min_size, ranges.

	memset(header_buffer, 0, header_size);
	memset(memory_buffer, 0, sizeof(DriftBuddyMemNode));

	DriftBuddyMem* mem = header_buffer;
	(*mem) = (DriftBuddyMem){
		.buffer = memory_buffer,
		.capacity = memory_size,
		.available = memory_size,
		.log2_max = DriftLog2Ceil(memory_size),
		.log2_min = DriftLog2Ceil(DRIFT_MEMORY_BLOCK_MIN_ALLOC_SIZE),
		.free_list[0] = memory_buffer,
	};
	
	// idx 0 is the buddy of idx 1 (the root node)
	// Set it to be allocated so the root can never be joined.
	DriftBuddyMemFlipAllocated(mem, 0);
	
	return mem;
}

DriftBuddyMem* DriftBuddyMemInit(const char* name, void* buffer, size_t size){
	size_t header_size = sizeof(DriftBuddyMem) + size/DRIFT_MEMORY_BLOCK_MIN_ALLOC_SIZE/4;
	uint8_t header_buffer[header_size];
	DriftBuddyMem* mem = DriftBuddyMemInitSeparate(name, header_buffer, header_size, buffer, size);

	// Allocate leaves to place the header in.
	int min_level = DriftBuddyMemLevelForSize(mem, DRIFT_MEMORY_BLOCK_MIN_ALLOC_SIZE);
	for(size_t alloc_size = 0; alloc_size < header_size; alloc_size += DRIFT_MEMORY_BLOCK_MIN_ALLOC_SIZE){
		DriftBuddyMemAllocBlock(mem, min_level);
	}

	memcpy(buffer, mem, header_size);
	return buffer;
}

void* DriftBuddyMemAlloc(DriftBuddyMem* mem, size_t size){
	if(size == 0){
		// Don't allocate empty blocks.
		return NULL;
	} else if(size < sizeof(DriftBuddyMemNode)){
		// Block smaller than memory nodes not allowed.
		size = sizeof(DriftBuddyMemNode);
	}

	return DriftBuddyMemAllocBlock(mem, DriftBuddyMemLevelForSize(mem, size));
}

void DriftBuddyMemDealloc(DriftBuddyMem* mem, void* ptr, size_t size){
	if(!ptr) return;

	int level = DriftBuddyMemLevelForSize(mem, size);
	DriftBuddyMemDeallocBlock(mem, ptr, level, DriftBuddyMemPtrToIndex(mem, ptr, level));
}

void* DriftBuddyMemRealloc(DriftBuddyMem* mem, void* ptr, size_t osize, size_t nsize){
	if(nsize == 0){
		// Special case: 0 size means deallocate.
		DriftBuddyMemDealloc(mem, ptr, osize);
		return NULL;
	}	if(ptr == NULL){
		// Special case: No existing pointer means allocate.
		return DriftBuddyMemAllocBlock(mem, DriftBuddyMemLevelForSize(mem, nsize));
	}

	int olevel = DriftBuddyMemLevelForSize(mem, osize);
	int nlevel = DriftBuddyMemLevelForSize(mem, nsize);
	
	// Recursively split the block if it's too big.
	while(olevel < nlevel){
		olevel++;
		DriftBuddyMemFlipAllocated(mem, DriftBuddyMemPtrToIndex(mem, ptr, olevel));
		DriftBuddyMemListPush(&mem->free_list[olevel], DriftBuddyMemBuddyPtr(mem, ptr, olevel));
		mem->available += DriftBuddyMemSizeForLevel(mem, olevel);
	}

	if(nlevel == olevel){
		// Block is (or was reduced to) the proper size already.
		return ptr;
	} else {
		// Reallocate and copy to grow.
		// TODO Is it worth checking to grow into buddy blocks?
		void* resized = DriftBuddyMemAllocBlock(mem, DriftBuddyMemLevelForSize(mem, nsize));
		memcpy(resized, ptr, nsize);
		DriftBuddyMemDeallocBlock(mem, ptr, olevel, DriftBuddyMemPtrToIndex(mem, ptr, olevel));
		return resized;
	}
}


// MARK: Strings

const char* DriftSMPrintf(DriftMem* mem, const char* format, ...){
	va_list args;
	
	va_start(args, format);
	// + 1 for the null terminator.
	size_t size = 1 + vsnprintf(NULL, 0, format, args);
	va_end(args);
	
	va_start(args, format);
	char* str = DriftAlloc(mem, size);
	size_t used = vsnprintf(str, size, format, args);
	DRIFT_ASSERT_WARN(size == used + 1, "DriftSMFormat() size mismatch?!");
	va_end(args);
	
	return str;
}

const char* DriftSMFormat(DriftMem* mem, const char* format, ...){
	va_list args;
	
	va_start(args, format);
	// + 1 for the null terminator.
	size_t size = 1 + DriftVSNFormat(NULL, 0, format, &args);
	va_end(args);
	
	va_start(args, format);
	char* str = DriftAlloc(mem, size);
	size_t used = DriftVSNFormat(str, size, format, &args);
	DRIFT_ASSERT_WARN(size == used + 1, "DriftSMFormat() size mismatch?!");
	va_end(args);
	
	return str;
}

// MARK: Stretchy Buffers

static inline size_t DriftStretchyBufferSize(DriftArray* header){
	return sizeof(*header) + header->capacity*header->elt_size;
}

void* _DriftArrayNew(DriftMem* mem, size_t capacity, size_t elt_size){
	if(capacity < DRIFT_STRETCHY_BUFER_MIN_CAPACITY) capacity = DRIFT_STRETCHY_BUFER_MIN_CAPACITY;
	DriftArray tmp = {._magic = 0xABCDABCDABCDABCD, .mem = mem, .capacity = capacity, .elt_size = elt_size};
	
	DriftArray* header = DriftAlloc(mem, DriftStretchyBufferSize(&tmp));
	*header = tmp;
	return header->array;
}

void DriftArrayFree(void* ptr){
	if(ptr){
		DriftArray* header = DriftArrayHeader(ptr);
		DriftDealloc(header->mem, header, DriftStretchyBufferSize(header));
	}
}

void* _DriftArrayGrow(void* ptr, size_t n_elt){
	DriftArray* header = DriftArrayHeader(ptr);
	size_t osize = DriftStretchyBufferSize(header);
	header->capacity = 3*(header->capacity + n_elt)/2;
	
	header = DriftRealloc(header->mem, header, osize, DriftStretchyBufferSize(header));
	return header->array;
}

void DriftArrayRangeCommit(void* ptr, void* cursor){
	DriftArray* header = DriftArrayHeader(ptr);
	header->count = (cursor - ptr)/header->elt_size;
	DRIFT_ASSERT(header->count <= header->capacity, "Buffer overrun detected.");
}
