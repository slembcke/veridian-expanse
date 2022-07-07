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

typedef struct {
	DriftTableDesc desc;
	size_t row_capacity, row_count, row_size;
	
	DriftName _names[1 + DRIFT_TABLE_MAX_COLUMNS];
	
	// Internal pointer to the table's memory.
	void* buffer;
} DriftTable;

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
