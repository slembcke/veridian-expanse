/*
This file is part of Veridian Expanse.

Veridian Expanse is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

Veridian Expanse is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with Veridian Expanse. If not, see <https://www.gnu.org/licenses/>.
*/

#include "string.h"

#include "drift_base.h"

static size_t TableSize(DriftTable* table){return table->row_capacity*table->row_size;}

DriftTable* DriftTableInit(DriftTable* table, DriftTableDesc desc){
	if(desc.name == NULL || desc.name[0] == '\0') desc.name = "<noname>";
	table->desc = desc;
	table->row_capacity = 0;
	table->row_count = 0;
	table->row_size = 0;
	
	table->row_capacity = DRIFT_MAX(DRIFT_TABLE_MIN_ROW_CAPACITY, -(-desc.min_row_capacity & -DRIFT_TABLE_MIN_ALIGNMENT));
	
	// Copy the name so static strings don't break hotloading.
	DriftName* name_cursor = table->_names;
	DriftNameCopy(name_cursor++, desc.name);
	table->desc.name = table->_names[0].str;
	
	DriftColumn *columns = table->desc.columns.arr;
	for(uint i = 0; i < DRIFT_TABLE_MAX_COLUMNS && columns[i].size; i++){
		table->row_size += columns[i].size;
	}
	
	table->buffer = DriftAlloc(table->desc.mem, TableSize(table));
	
	// Init column pointers.
	void* cursor = table->buffer;
	for(uint i = 0; i < DRIFT_TABLE_MAX_COLUMNS && columns[i].size; i++){
		columns[i].ptr = *columns[i].user = cursor;
		cursor += table->row_capacity*columns[i].size;
		
		DriftNameCopy(name_cursor, columns[i].name);
		columns[i].name = name_cursor->str;
		name_cursor++;
	}
	
	return table;
}

void DriftTableDestroy(DriftTable* table){
	DriftDealloc(table->desc.mem, table->buffer, TableSize(table));
	table->buffer = NULL;
	table->row_capacity = 0;
}

void DriftTableIO(DriftTable* table, DriftIO* io){
	// Handle the row count.
	DriftIOBlock(io, table->desc.name, &table->row_count, sizeof(table->row_count));
	// Handle the capacity.
	size_t capacity = table->row_capacity;
	DriftIOBlock(io, table->desc.name, &capacity, sizeof(capacity));
	if(io->read) DriftTableEnsureCapacity(table, capacity);
	
	// Handle the columns.
	DriftColumn* columns = table->desc.columns.arr;
	for(uint i = 0; i < DRIFT_TABLE_MAX_COLUMNS && columns[i].size; i++){
		DriftIOBlock(io, columns[i].name, columns[i].ptr, columns[i].size*table->row_count);
	}
}

void DriftTableResize(DriftTable* table, size_t row_capacity){
	DRIFT_ASSERT(table->row_capacity <= row_capacity, "NYI Cannot shrink DriftTable.");
	
	DriftTable copy = *table;
	
	// Re-init the table with the new minimum row count.
	table->desc.min_row_capacity = row_capacity;
	DriftTableInit(table, table->desc);
	
	// Copy data to new table.
	table->row_count = copy.row_count;
	DriftColumn* src = copy.desc.columns.arr;
	DriftColumn* dst = table->desc.columns.arr;
	for(uint i = 0; i < DRIFT_TABLE_MAX_COLUMNS && src[i].size; i++){
		memcpy(dst[i].ptr, src[i].ptr, copy.row_capacity*src[i].size);
	}
	
	DRIFT_LOG("DriftTable '%s' resized from %d to %d", table->desc.name, copy.row_capacity, table->row_capacity);
	
	// Delete the old table.
	DriftTableDestroy(&copy);
}

void DriftTableClearRow(DriftTable* table, uint idx){
	DriftTableCopyRow(table, idx, 0);
	// DriftColumn *columns = table->desc.columns.arr;
	// for(uint i = 0; i < DRIFT_TABLE_MAX_COLUMNS && columns[i].size; i++){
	// 	memset(columns[i].ptr + idx*columns[i].size, 0, columns[i].size);
	// }
}

void DriftTableCopyRow(DriftTable* table, uint dst_idx, uint src_idx){
	DriftColumn* columns = table->desc.columns.arr;
	for(uint i = 0; i < DRIFT_TABLE_MAX_COLUMNS && columns[i].size; i++){
		void* ptr = columns[i].ptr;
		uint size = columns[i].size;
		memcpy(ptr + size*dst_idx, ptr + size*src_idx, size);
	}
}

// TODO Unit Tests
