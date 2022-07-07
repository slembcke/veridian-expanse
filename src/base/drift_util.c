#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#include "tina/tina.h"
#include "miniz/miniz.h"

#include "drift_types.h"
#include "drift_math.h"
#include "drift_util.h"
#include "drift_mem.h"
#include "drift_app.h"

static mtx_t log_mtx;
static FILE* log_out;
static FILE* log_err;

#if __WIN64__
static LARGE_INTEGER queryPerfFreq;
#endif

uint DRIFT_SRC_PREFIX_LENGTH = 0;
void DriftUtilInit(void){
	DRIFT_SRC_PREFIX_LENGTH = strlen(__FILE__) - strlen("src/base/drift_util.c");
	
	mtx_init(&log_mtx, mtx_plain);
	log_out = stdout;
	log_err = stderr;
	
#if __WIN64__
	// logout = logerr = fopen("log", "w");
	QueryPerformanceFrequency(&queryPerfFreq);
#endif
}

typedef struct {
	const char* const read;
	char* const write;
	size_t read_idx, write_idx, size;
} format_cursor;

static const char* format_match(format_cursor *curs, const char* match){
	size_t len = strlen(match);
	if(strncmp(curs->read + curs->read_idx, match, len) == 0){
		curs->read_idx += len;
		const char* args = curs->read + curs->read_idx;
		
		do { // Advance past matching bracket
			DRIFT_ASSERT(curs->read[curs->read_idx] != 0, "Unterminated format bracket.");
		} while(curs->read[curs->read_idx++] != '}');
		
		return args;
	} else {
		return NULL;
	}
}

static inline void format_putc(format_cursor* curs, char c){
	if(curs->write == NULL){
		// Count only, no output.
	} else if(curs->write_idx < curs->size - 1){
		curs->write[curs->write_idx] = c;
	} else {
		curs->write[curs->size - 1] = 0;
	}
	
	curs->write_idx++;
}

static void format_sprintf(format_cursor* curs, const char* opts, const char* format, ...){
	char _format[64] = "";
	format_cursor _curs = {.read = format, .write = _format, .size = sizeof(_format)};
	
	// Substitute '@' for opts.
	while(_curs.read[_curs.read_idx]){
		if(_curs.read[_curs.read_idx] == '@'){
			_curs.read_idx++;
			
			const char* _opts = opts;
			while(*_opts != '}') format_putc(&_curs, *_opts++);
		} else {
			format_putc(&_curs, _curs.read[_curs.read_idx++]);
		}
	}
	
	// Sprintf
	va_list args; va_start(args, format);
	size_t size = (curs->size > curs->write_idx ? curs->size - curs->write_idx : 0);
	int count = vsnprintf(curs->write + curs->write_idx, size, _format, args);
	curs->write_idx += count;
	va_end(args);
}

static bool format_int(format_cursor* curs, va_list* args){
	const char* opts = format_match(curs, "i:");
	if(opts) format_sprintf(curs, opts, "%@d", va_arg(*args, int));
	
	return opts != NULL;
}

static bool format_float(format_cursor* curs, va_list* args){
	const char* opts = format_match(curs, "f:");
	if(opts) format_sprintf(curs, opts, "%@f", va_arg(*args, double));
	
	return opts != NULL;
}

static bool format_vec2(format_cursor* curs, va_list* args){
	const char* opts = format_match(curs, "v2:");
	if(opts){
		DriftVec2 v = va_arg(*args, DriftVec2);
		format_sprintf(curs, opts, "(%@f, %@f)", v.x, v.y);
	}
	
	return opts != NULL;
}

static bool format_vec3(format_cursor* curs, va_list* args){
	const char* opts = format_match(curs, "v3:");
	if(opts){
		DriftVec3 v = va_arg(*args, DriftVec3);
		format_sprintf(curs, opts, "(%@f, %@f, %@f)", v.x, v.y, v.z);
	}
	
	return opts != NULL;
}

typedef bool format_func(format_cursor* curs, va_list* args);
static format_func* FORMAT_FUNCS[] = {
	format_int,
	format_float,
	format_vec2,
	format_vec3,
	NULL,
};

static void format_apply(format_cursor* curs, va_list* args){
	for(int i = 0; FORMAT_FUNCS[i]; i++){
		if(FORMAT_FUNCS[i](curs, args)) return;
	}
	
	DRIFT_ABORT("Invalid format string: '%s'", curs->read);
}

size_t DriftVSNFormat(char* buffer, size_t size, const char* format, va_list* args){
	format_cursor curs = {.read = format, .write = buffer, .size = size};
	
	while(true){
		char c = curs.read[curs.read_idx];
		
		if(
			// Check if we are starting a format string.
			c == '{' &&
			// Consume the opening bracket, check if it was just a literal => '{{'
			curs.read[++curs.read_idx] != '{'
		){
			format_apply(&curs, args);
		} else {
			format_putc(&curs, c);
			curs.read_idx++;
		}
		
		// Check if the string was terminated.
		if(c == '\0') return curs.write_idx - 1;
	}
}

size_t DriftSNFormat(char* buffer, size_t size, const char* format, ...){
	va_list args; va_start(args, format);
	size_t used = DriftVSNFormat(buffer, size, format, &args);
	va_end(args);
	return used;
}

void DriftLogf(const char *format, const char *file, unsigned line, const char *message, ...){
	char message_buffer[4096];
	
	va_list args; va_start(args, message);
	size_t used = DriftVSNFormat(message_buffer, sizeof(message_buffer), message, &args);
	va_end(args);
	
	mtx_lock(&log_mtx);
	fprintf(log_out, format, file + DRIFT_SRC_PREFIX_LENGTH, line, message_buffer);
	fflush(log_out);
	mtx_unlock(&log_mtx);
}

void DriftLog(const char *format, const char *file, unsigned line, const char *message, ...){
	char message_buffer[4096];
	
	va_list vargs;
	va_start(vargs, message); {
		vsnprintf(message_buffer, sizeof(message_buffer), message, vargs);
	} va_end(vargs);
	
	mtx_lock(&log_mtx);
	fprintf(log_out, format, file + DRIFT_SRC_PREFIX_LENGTH, line, message_buffer);
	fflush(log_out);
	mtx_unlock(&log_mtx);
}

void DriftNameCopy(DriftName* dst, const char* src){
	if(dst->str != src) strncpy(dst->str, src, DRIFT_NAME_SIZE - 1);
}

DriftStopwatch _DriftStopwatchStart(const char* label){
	DriftStopwatch sw = {};
	_DriftStopwatchMark(&sw, label);
	return sw;
}

void _DriftStopwatchMark(DriftStopwatch* sw, const char* label){
	sw->events[sw->count++] = (DriftStopwatchEvent){.label = label, .time = DriftTimeNanos()};
}

void _DriftStopwatchStop(DriftStopwatch* sw, const char* label, const char* file, uint line){
	_DriftStopwatchMark(sw, label);
	
	char buffer[1024];
	char* cursor = buffer;
	
	DriftStopwatchEvent* events = sw->events;
	for(uint i = 1; i < sw->count; i++){
		cursor += sprintf(cursor, "%s: %5.1f, ", events[i].label, (events[i].time - events[i - 1].time)/1e3);
	}
	
	DriftLog("[PROFILE] %s:%d: %s\n", file, line, "%s: %6.1f us, {%s}", events[0].label, (events[sw->count - 1].time - events[0].time)/1e3, buffer);
}

void DriftBreakpoint(){}
void DriftAbort(){abort();}

void DriftAssertHelper(const char *condition, const char *file, unsigned line, DriftAssertType type, const char *message, ...){
	char message_buffer[1024];

	va_list vargs;
	va_start(vargs, message); {
		vsnprintf(message_buffer, sizeof(message_buffer), message, vargs);
	} va_end(vargs);
	
	const char *message_type = (type != DRIFT_ASSERT_WARN ? "Aborting due to error" : "Warning");
	
	mtx_lock(&log_mtx);
	fprintf(log_err,
		"%s: %s\n"
		"\tFailed condition: %s\n"
		"\tSource: %s:%d\n",
		message_type, message_buffer, condition, file + DRIFT_SRC_PREFIX_LENGTH, line
	);
	fflush(log_err);
	mtx_unlock(&log_mtx);
	
	// Put breakpoints here:
	switch(type){
		case DRIFT_ASSERT_ERROR: break;
		case DRIFT_ASSERT_HARD: break;
		case DRIFT_ASSERT_WARN: break;
	}
}

void DriftIOBlock(DriftIO* io, const char* label, void* ptr, size_t size){
	DriftData data = {ptr, size};
	// DRIFT_LOG("block '%s' %p %u*%u", label, ptr, size, count);
	if(size) tina_yield(io->_coro, &data);
}

void* io_body(tina* coro, void* value){
	DriftIO* io = coro->user_data;
	io->_io_func(io);
	return false;
}

#define IO_FOREACH(_io_func_, _user_ptr_, _read_, _var_) \
	DriftIO _io_ = {._io_func = _io_func_, .user_ptr = _user_ptr_, .read = _read_}; \
	u8 _io_buffer[64*1024]; \
	_io_._coro = tina_init(_io_buffer, sizeof(_io_buffer), io_body, &_io_); \
	for(DriftData* _var_; (_var_ = (DriftData*)tina_resume(_io_._coro, 0));)

size_t DriftIOSize(DriftIOFunc* io_func, void* user_ptr){
	size_t size = 0;
	
	IO_FOREACH(io_func, user_ptr, true, data){
		size += data->size;
	}
	
	return size;
}

bool DriftIOFileRead(const char* filename, DriftIOFunc* io_func, void* user_ptr){
	FILE* file = fopen(filename, "rb");
	if(file == NULL) return false;
	
	IO_FOREACH(io_func, user_ptr, true, data){
		DRIFT_ASSERT_HARD(fread(data->ptr, data->size, 1, file) == 1, "Failed to read block.");
	}
	
	// DRIFT_LOG("Read '%s'.", filename);
	fclose(file);
	return true;
}

void DriftIOFileWrite(const char* filename, DriftIOFunc* io_func, void* user_ptr){
	FILE* file = fopen(filename, "wb");
	DRIFT_ASSERT_HARD(file, "Failed to open '%s' for writing.", filename);
	
	IO_FOREACH(io_func, user_ptr, false, data){
		DRIFT_ASSERT_HARD(fwrite(data->ptr, data->size, 1, file) == 1, "Failed to write block.");
	}
	
	DRIFT_LOG("Wrote '%s'.", filename);
	fclose(file);
}

static mz_zip_archive ZipHandles[DRIFT_APP_MAX_THREADS];
static const char* ResourcesZipName = "resources.zip";
static mtx_t DriftAssetMutex;
static DriftLinearMem* DriftAssetMem;
static char ASSET_BUFFER[256*1024*1024];

static const char* zip_error(mz_zip_archive* zip){return mz_zip_get_error_string(mz_zip_get_last_error(zip));}

const DriftConstData* DriftAssetGet(const char* filename){
	mz_zip_archive* zip = ZipHandles + DriftAppGetThreadID();
	
	int idx = mz_zip_reader_locate_file(zip, filename, NULL, 0);
	DRIFT_ASSERT_HARD(idx >= 0, "Asset '%s/%s' not found.", ResourcesZipName, filename);
	if(idx < 0) return NULL;
	
	mz_zip_archive_file_stat stat;
	bool success = mz_zip_reader_file_stat(zip, idx, &stat);
	DRIFT_ASSERT_HARD(success, "mz_zip_reader_file_stat() failed: %s", zip_error(zip));
	size_t size = stat.m_uncomp_size;
	
	mtx_lock(&DriftAssetMutex);
	void* buffer = DriftLinearMemAlloc(DriftAssetMem, size);
	DriftConstData* asset = DriftLinearMemAlloc(DriftAssetMem, sizeof(*asset));
	DRIFT_ASSERT_HARD(buffer && asset, "Ran out of asset memory.");
	(*asset) = (DriftConstData){.data = buffer, .length = size};
	mtx_unlock(&DriftAssetMutex);
	
	u8 tmp[64*1024];
	success = mz_zip_reader_extract_to_mem_no_alloc(zip, idx, buffer, size, 0, tmp, sizeof(tmp));
	DRIFT_ASSERT_HARD(success, "Failed to extract '%s/%s': %s", ResourcesZipName, filename, zip_error(zip));
	
	return asset;
}

void DriftAssetsReset(void){
	mtx_destroy(&DriftAssetMutex);
	mtx_init(&DriftAssetMutex, mtx_plain);
	DriftAssetMem = DriftLinearMemInit(ASSET_BUFFER, sizeof(ASSET_BUFFER));
	
	FILE* file = fopen(ResourcesZipName, "rb");
	DRIFT_ASSERT_HARD(file, "Failed to open '%s'", ResourcesZipName);
	DRIFT_ASSERT_HARD(fseek(file, 0, SEEK_END) == 0, "Failed to get '%s' size.", ResourcesZipName);
	size_t resources_size = ftell(file);
	
	void* resource_buffer = DriftLinearMemAlloc(DriftAssetMem, resources_size);
	DRIFT_ASSERT_HARD(resource_buffer, "Failed to allocate for '%s'.", ResourcesZipName);
	
	DRIFT_ASSERT_HARD(fseek(file, 0, SEEK_SET) == 0, "Failed to get '%s' size.", ResourcesZipName);
	DRIFT_ASSERT_HARD(fread(resource_buffer, resources_size, 1, file), "Failed to read '%s'.", ResourcesZipName);
	fclose(file);
	
	mz_zip_archive* zip = ZipHandles + 0;
	if(zip->m_pState) mz_zip_reader_end(zip);
	bool success = mz_zip_reader_init_mem(zip, resource_buffer, resources_size, 0);
	DRIFT_ASSERT_HARD(success, "Failed to load '%s': %s.", ResourcesZipName, zip_error(zip));
	
	// Copy handles so each thread gets it's own error handles and whatnot.
	for(uint i = 1; i < DRIFT_APP_MAX_THREADS; i++) ZipHandles[i] = ZipHandles[0];
}

u64 DriftNextPOT(u64 n){
	n--;
	n |= n >> 1;
	n |= n >> 2;
	n |= n >> 4;
	n |= n >> 8;
	n |= n >> 16;
	n |= n >> 32;
	return n + 1;
}

uint DriftFindFirstBit(u64 bits){
	uint idx = 0, shift = 32;
	u64 mask = 0xFFFFFFFF;

	while((bits & 1) == 0){
		if((bits & mask) == 0){
			bits >>= shift;
			idx += shift;
		}
		
		shift >>= 1;
		mask >>= shift;
	}

	return idx;
}

uint DriftLog2Ceil(u64 n){
	n -= 1;
	if(n == 0) return 64;
	int z = 0;
	if((n & 0xFFFFFFFF00000000) == 0) z += 32, n <<= 32;
	if((n & 0xFFFF000000000000) == 0) z += 16, n <<= 16;
	if((n & 0xFF00000000000000) == 0) z +=  8, n <<=  8;
	if((n & 0xF000000000000000) == 0) z +=  4, n <<=  4;
	if((n & 0xC000000000000000) == 0) z +=  2, n <<=  2;
	if((n & 0x8000000000000000) == 0) z +=  1;
	return 64 - z;
}

u64 DriftTimeNanos(void){
#if __unix__ || __APPLE__
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	return 1000000000*(u64)now.tv_sec + (u64)now.tv_nsec;
#elif __WIN64__
	LARGE_INTEGER now;
	QueryPerformanceCounter(&now);
	
	u64 hi = (now.QuadPart >> 32)*1000000000/queryPerfFreq.QuadPart;
	u64 lo = (now.QuadPart & 0xFFFFFFFF)*1000000000/queryPerfFreq.QuadPart;
	return (hi << 32) + lo;
#else
	#error Unhandled platform
	return 0;
#endif
}

#if DRIFT_DEBUG
void unit_test_util(void){
	DRIFT_ASSERT(DriftNextPOT(0) == 0, "Invalid result");
	DRIFT_ASSERT(DriftNextPOT(1) == 1, "Invalid result");
	DRIFT_ASSERT(DriftNextPOT(2) == 2, "Invalid result");
	DRIFT_ASSERT(DriftNextPOT(3) == 4, "Invalid result");
	DRIFT_ASSERT(DriftNextPOT(4) == 4, "Invalid result");
	DRIFT_ASSERT(DriftNextPOT(5) == 8, "Invalid result");
	DRIFT_ASSERT(DriftNextPOT(1024) == 1024, "Invalid result");
	DRIFT_ASSERT(DriftNextPOT(1025) == 2048, "Invalid result");
	
	// DRIFT_ASSERT(DriftLog2Ceil(1) == 0, "Invalid result");
	DRIFT_ASSERT(DriftLog2Ceil(2) == 1, "Invalid result");
	DRIFT_ASSERT(DriftLog2Ceil(4) == 2, "Invalid result");
	DRIFT_ASSERT(DriftLog2Ceil(8) == 3, "Invalid result");
	DRIFT_ASSERT(DriftLog2Ceil(255) == 8, "Invalid result");
	DRIFT_ASSERT(DriftLog2Ceil(256) == 8, "Invalid result");
	DRIFT_ASSERT(DriftLog2Ceil((1 << 20) - 1) == 20, "Invalid result");
	
	DRIFT_LOG("Util tests passed.");
}
#endif
