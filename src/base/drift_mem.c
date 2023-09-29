/*
This file is part of Veridian Expanse.

Veridian Expanse is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

Veridian Expanse is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with Veridian Expanse. If not, see <https://www.gnu.org/licenses/>.
*/

#include <stdalign.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <SDL.h>

#include "drift_base.h"

#if DRIFT_SANITIZE
#include <sanitizer/asan_interface.h>
#else
#define ASAN_POISON_MEMORY_REGION(...)
#define ASAN_UNPOISON_MEMORY_REGION(...)
#endif

typedef void* DriftMemFunc(DriftMem* mem, void* ptr, size_t osize, size_t nsize);

struct DriftMem {
	DriftMemFunc* func;
	char const* label;
};

void* DriftAlloc(DriftMem* mem, size_t size){return mem->func(mem, NULL, 0, size);}
void* DriftRealloc(DriftMem* mem, void* ptr, size_t osize, size_t nsize){
	if(ptr == NULL || osize == 0) DRIFT_ASSERT_WARN(ptr == NULL && osize == 0, "Invalid realloc params.");
	// DRIFT_LOG_DEBUG("Realloc from %u to %u bytes.", osize, nsize);
	return mem->func(mem, ptr, osize, nsize);
}
void DriftDealloc(DriftMem* mem, void* ptr, size_t size){if(ptr) mem->func(mem, ptr, size, 0);}

static void* DriftSystemMemFunc(DriftMem* mem, void* ptr, size_t osize, size_t nsize){
	if(osize == 0){
		DRIFT_ASSERT(nsize < 1024*1024*1024, "Huge allocation detected!");
		return calloc(1, nsize);
	} else if(nsize == 0){
		free(ptr);
		return NULL;
	} else {
		return realloc(ptr, nsize);
	}
}

DriftMem* const DriftSystemMem = &(DriftMem){
	.func = DriftSystemMemFunc,
	.label= "DriftSystemMemory",
};


// MARK: Linear Allocator.

typedef struct DriftLinearMem {
	DriftMem mem;
	uintptr_t buffer, cursor;
} DriftLinearMem;

static void* DriftLinearMemAlloc(DriftLinearMem* mem, size_t size){
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

static void* DriftLinearMemFunc(DriftMem* mem, void* ptr, size_t osize, size_t nsize){
	if(nsize <= osize){
		// If shrinking or dellocating, do nothing.
		return (nsize > 0 ? ptr : NULL);
	} else {
		void* new_ptr = DriftLinearMemAlloc((DriftLinearMem*)mem, nsize);
		// Copy to new ptr if grown.
		return (osize > 0 ? memcpy(new_ptr, ptr, osize) : new_ptr);
	}
}

static DriftLinearMem* _DriftLinearMemMake(void* buffer, size_t size, const char* label){
	DriftLinearMem tmp = {.buffer = (uintptr_t)buffer, .cursor = (uintptr_t)buffer + size};
	// Allocate the header from the block, copy, and return it.
	DriftLinearMem* mem = DriftLinearMemAlloc(&tmp, sizeof(tmp));
	DRIFT_ASSERT_HARD(mem, "Could not allocate linear allocator header for: '%s'", label);
	
	tmp.mem = (DriftMem){.func = DriftLinearMemFunc, .label = "DriftLinearMem"};
	(*mem) = tmp;
	return mem;
}

DriftMem* DriftLinearMemMake(void* buffer, size_t size, const char* label){
	return &_DriftLinearMemMake(buffer, size, label)->mem;
}


// MARK: List mem

typedef struct {
	DriftMem mem;
	DriftMap map;
} DriftListMem;

static void* DriftListMemFunc(DriftMem* mem, void* ptr, size_t osize, size_t nsize){
	DriftMap* map = &((DriftListMem*)mem)->map;
	DriftMem* parent_mem = map->table.desc.mem;
	
	if(osize == 0){
		void* ptr = DriftAlloc(parent_mem, nsize);
		DriftMapInsert(map, (uintptr_t)ptr, nsize);
		return ptr;
	} else if(nsize == 0){
		DRIFT_ASSERT_HARD(DriftMapRemove(map, (uintptr_t)ptr) == osize, "Allocation size mismatch.");
		DriftDealloc(parent_mem, ptr, osize);
		return NULL;
	} else {
		DRIFT_ASSERT_HARD(DriftMapRemove(map, (uintptr_t)ptr) == osize, "Allocation size mismatch.");
		ptr = DriftRealloc(parent_mem, ptr, osize, nsize);
		DriftMapInsert(map, (uintptr_t)ptr, nsize);
		return ptr;
	}
	
	DRIFT_ASSERT_HARD(false, "Allocation not found!");
}

DriftMem* DriftListMemNew(DriftMem* parent_mem, const char* label){
	DriftListMem* mem = DRIFT_COPY(parent_mem, ((DriftListMem){.mem.func = DriftListMemFunc, .mem.label = label}));
	DriftMapInit(&mem->map, parent_mem, "ListMem Map", 0);
	return &mem->mem;
}

void DriftListMemFree(DriftMem* mem){
	DRIFT_ASSERT_HARD(mem->func == DriftListMemFunc, "Wrong memory type.");
	DriftMap* map = &((DriftListMem*)mem)->map;
	DriftMem* parent_mem = map->table.desc.mem;
	
	for(uint i = 0; i < map->table.row_capacity; i++){
		if(DriftMapActiveIndex(map, i)) DriftDealloc(parent_mem, (void*)map->keys[i], map->values[i]);
	}
	DriftMapDestroy(map);
	
	DriftDealloc(parent_mem, mem, sizeof(DriftListMem));
}


// MARK: Zone Allocator.

#define MAX_BLOCKS 256
#define MAX_ZONES 8
#define BLOCK_SIZE (1 << 20)

typedef struct {
	DriftMem mem;
	DriftZoneMemHeap* parent_heap;
	DriftLinearMem* current_allocator[DRIFT_APP_MAX_THREADS];
	
	uint block_count;
	void* blocks[MAX_BLOCKS];
} DriftZone;

struct DriftZoneMemHeap {
	const char* label;
	DriftMem* parent_mem;
	SDL_mutex* lock;
	
	DRIFT_ARRAY(void*) blocks;
	DRIFT_ARRAY(void*) pooled_blocks;
	
	DriftZone zones[MAX_ZONES];
	DRIFT_ARRAY(DriftZone*) pooled_zones;
};

static void* alloc_block(DriftZoneMemHeap* heap){
	DriftArray* header = DriftArrayHeader(heap->blocks);
	DRIFT_ASSERT_HARD(header->count < header->capacity, "Zone heap '%s' is full!", heap->label);
	
	void* block = DriftAlloc(heap->parent_mem, BLOCK_SIZE);
	DRIFT_ASSERT_HARD(block, "Zone heap '%s' failed to allocate block.", heap->label);
	
	DRIFT_ARRAY_PUSH(heap->blocks, block);
	return block;
}

static void* get_block(DriftZoneMemHeap* heap){
	SDL_LockMutex(heap->lock);
	if(DriftArrayLength(heap->pooled_blocks) == 0){
		// Allocate a new one if there are no free blocks.
		DRIFT_ARRAY_PUSH(heap->pooled_blocks, alloc_block(heap));
		DriftArray* header = DriftArrayHeader(heap->blocks);
		DRIFT_LOG("Zone heap '%s' allocated new block. (%d/%d)", heap->label, header->count, header->capacity);
	}
	
	void* block = DRIFT_ARRAY_POP(heap->pooled_blocks, NULL);
	SDL_UnlockMutex(heap->lock);
	
	return block;
}

DriftZoneMemHeap* DriftZoneMemHeapNew(DriftMem* mem, const char* label){
	DriftZoneMemHeap* heap = DRIFT_COPY(mem, ((DriftZoneMemHeap){
		.label = label, .parent_mem = mem,
		.blocks = DRIFT_ARRAY_NEW(mem, MAX_BLOCKS, void*),
		.pooled_blocks = DRIFT_ARRAY_NEW(mem, MAX_BLOCKS, void*),
		.pooled_zones = DRIFT_ARRAY_NEW(mem, MAX_ZONES, DriftZone*),
	}));
	
	// Pool the zones.
	for(uint i = 0; i < MAX_ZONES; i++) DRIFT_ARRAY_PUSH(heap->pooled_zones, heap->zones + i);
	
	// Allocate some initial blocks for the pool.
	for(uint i = 0; i < 16; i++) DRIFT_ARRAY_PUSH(heap->pooled_blocks, alloc_block(heap));
	
	heap->lock = SDL_CreateMutex();
	return heap;
}

void DriftZoneMemHeapFree(DriftZoneMemHeap* heap){
	SDL_DestroyMutex(heap->lock);
	DRIFT_ARRAY_FOREACH(heap->blocks, block) DriftDealloc(heap->parent_mem, *block, BLOCK_SIZE);
	DriftArrayFree(heap->blocks);
	DriftArrayFree(heap->pooled_blocks);
	DriftArrayFree(heap->pooled_zones);
	DriftDealloc(DriftSystemMem, heap, sizeof(*heap));
}

DriftZoneHeapInfo DriftZoneHeapGetInfo(DriftZoneMemHeap* heap){
	uint blocks = DriftArrayLength(heap->blocks);
	DriftZoneHeapInfo info = {
		.blocks_allocated = blocks, .blocks_used = blocks - DriftArrayLength(heap->pooled_blocks),
		.zones_allocated = MAX_ZONES, .zones_used = MAX_ZONES - DriftArrayLength(heap->pooled_zones),
	};
	
	for(uint i = 0; i < MAX_ZONES; i++) info.zone_names[i] = heap->zones[i].mem.label;
	
	return info;
}

static void* DriftZoneMemAlloc(DriftZone* zone, size_t size){
	uint thread_id = DriftGetThreadID();
	DRIFT_ASSERT_HARD(thread_id < DRIFT_APP_MAX_THREADS, "DriftZoneMalloc(): Invalid thread id.");
	
	// Fast path when there is an allocator with enough space.
	if(zone->current_allocator[thread_id]){
		void* ptr = DriftLinearMemAlloc(zone->current_allocator[thread_id], size);
		if(ptr) return ptr;
	}
	
	size_t block_size = BLOCK_SIZE;
	DRIFT_ASSERT(size < block_size, "Allocation size exceeds block size.");
	void* block = get_block(zone->parent_heap);
	
	// Claim the block.
	zone->blocks[zone->block_count++] = block;
	// Initialize the allocator.
	ASAN_UNPOISON_MEMORY_REGION(block + block_size - sizeof(DriftLinearMem), sizeof(DriftLinearMem));
	zone->current_allocator[thread_id] = _DriftLinearMemMake(block, block_size, zone->mem.label);
	return DriftLinearMemAlloc(zone->current_allocator[thread_id], size);
}

static void* DriftZoneMemFunc(DriftMem* mem, void* ptr, size_t osize, size_t nsize){
	if(nsize <= osize){
		// If shrinking or dellocating, do nothing.
		return (nsize > 0 ? ptr : NULL);
	} else {
		// Combined alloc/grow.
		void* new_ptr = DriftZoneMemAlloc((DriftZone*)mem, nsize);
		DRIFT_ASSERT_HARD(new_ptr, "Failed to resize memory for '%s'", mem->label);
		
		ASAN_UNPOISON_MEMORY_REGION(new_ptr, nsize);
		return (osize > 0 ? memcpy(new_ptr, ptr, osize) : new_ptr);
	}
}

DriftMem* DriftZoneMemAquire(DriftZoneMemHeap* heap, const char* label){
	SDL_LockMutex(heap->lock);
	DriftZone* zone = DRIFT_ARRAY_POP(heap->pooled_zones, NULL);
	SDL_UnlockMutex(heap->lock);
	
	DRIFT_ASSERT(zone, "Ran out of pooled zones.");
	*zone = (DriftZone){.mem.func = DriftZoneMemFunc, .mem.label = label, .parent_heap = heap};
	// DRIFT_LOG("zone aquired '%s' %p", label, zone);
	return &zone->mem;
}

void DriftZoneMemRelease(DriftMem* mem){
	DRIFT_ASSERT(mem->func == DriftZoneMemFunc, "Invalid zone mem object.");
	DriftZone* zone = (DriftZone*)mem;
	// DRIFT_LOG("releasing zone: '%s' %p", mem->label, zone);
	DriftZoneMemHeap* heap = zone->parent_heap;
	
	// Re-pool the resources.
	zone->mem.label = "<pooled>";
	SDL_LockMutex(heap->lock);
	for(uint i = 0; i < zone->block_count; i++) ASAN_POISON_MEMORY_REGION(zone->blocks[i], BLOCK_SIZE);
	while(zone->block_count--) DRIFT_ARRAY_PUSH(heap->pooled_blocks, zone->blocks[zone->block_count]);
	DRIFT_ARRAY_PUSH(heap->pooled_zones, zone);
	SDL_UnlockMutex(heap->lock);
}


// MARK: Strings

char* DriftSMPrintf(DriftMem* mem, const char* format, ...){
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

char* DriftSMFormat(DriftMem* mem, const char* format, ...){
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
	
	size_t size = DriftStretchyBufferSize(&tmp);
	DriftArray* header = DriftAlloc(mem, size);
	memset(header, 0, size);
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

void _DriftArrayShiftUp(void* ptr, unsigned idx){
	DriftArray* header = DriftArrayHeader(ptr);
	size_t elt_size = header->elt_size;
	memmove(ptr + (idx + 1)*elt_size, ptr + (idx + 0)*elt_size, (header->count - idx)*elt_size);
	header->count++;
}

void DriftArrayTruncate(void* ptr, size_t len){
	DriftArray* header = DriftArrayHeader(ptr);
	if(header->count > len) header->count = len;
}

void DriftArrayRangeCommit(void* ptr, void* cursor){
	DriftArray* header = DriftArrayHeader(ptr);
	header->count = (cursor - ptr)/header->elt_size;
	DRIFT_ASSERT(header->count <= header->capacity, "Buffer overrun detected.");
}

/*
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
*/
