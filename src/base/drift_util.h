#pragma once

#include <stdarg.h>

typedef struct tina tina;

void DriftUtilInit(void);

extern uint DRIFT_SRC_PREFIX_LENGTH;

size_t DriftSNFormat(char* buffer, size_t size, const char* format, ...);
size_t DriftVSNFormat(char* buffer, size_t size, const char* format, va_list* args);

void DriftLogf(const char *format, const char *file, uint line, const char *message, ...);
void DriftLog(const char *format, const char *file, uint line, const char *message, ...);
void DriftBreakpoint(void);
_Noreturn void DriftAbort(void);

typedef enum {DRIFT_ASSERT_WARN, DRIFT_ASSERT_ERROR, DRIFT_ASSERT_HARD} DriftAssertType;
void DriftAssertHelper(const char *condition, const char *file, uint line, DriftAssertType type, const char *message, ...);

#define DRIFT_VAR(_var_, _expr_) typeof(_expr_) _var_ = (_expr_)

#define DRIFT_NAME_SIZE 32
typedef struct {char str[DRIFT_NAME_SIZE];} DriftName;
void DriftNameCopy(DriftName* dst, const char* src);

#define DRIFT_STR(_S_) #_S_
#define DRIFT_XSTR(_S) DRIFT_STR(_S)

typedef struct {
	const char* label;
	u64 time;
} DriftStopwatchEvent;

#define DRIFT_STOPWATCH_MAX_EVENTS 8
typedef struct {
	DriftStopwatchEvent events[DRIFT_STOPWATCH_MAX_EVENTS];
	uint count;
} DriftStopwatch;

DriftStopwatch _DriftStopwatchStart(const char* label);
void _DriftStopwatchMark(DriftStopwatch* sw, const char* label);
void _DriftStopwatchStop(DriftStopwatch* sw, const char* label, const char* file, uint line);

#ifndef NDEBUG
	#define DRIFT_DEBUG 1
	#define DRIFT_BREAKPOINT(_cond_) if(!(_cond_)) DriftBreakpoint();
	#define DRIFT_LOG_DEBUG(...) DriftLog("[DEBUG] %s:%d: %s\n", __FILE__, __LINE__, __VA_ARGS__)
	#define DRIFT_ASSERT(_condition_, ...) if(!(_condition_)){DriftAssertHelper(#_condition_, __FILE__, __LINE__, DRIFT_ASSERT_ERROR, __VA_ARGS__); DriftAbort();}
	#define DRIFT_ASSERT_WARN(_condition_, ...) if(!(_condition_)) DriftAssertHelper(#_condition_, __FILE__, __LINE__, DRIFT_ASSERT_WARN, __VA_ARGS__)
	#define DRIFT_UNREACHABLE() DRIFT_ASSERT(false, "This should be unreachable.");
#else
	#define DRIFT_DEBUG 0
	#define DRIFT_BREAKPOINT(cond){}
	#define DRIFT_LOG_DEBUG(...){}
	#define	DRIFT_ASSERT(_condition_, ...){}
	#define	DRIFT_ASSERT_WARN(_condition_, ...){}
	#define DRIFT_UNREACHABLE() __builtin_unreachable()
#endif

#define DRIFT_STOPWATCH_START(_label_) _DriftStopwatchStart(_label_)
#define DRIFT_STOPWATCH_MARK(_sw_, _label_) _DriftStopwatchMark(&_sw_, _label_)
#define DRIFT_STOPWATCH_STOP(_sw_, _label_) _DriftStopwatchStop(&_sw_, _label_, __FILE__, __LINE__)

#define DRIFT_LOGF(...) DriftLogf("[LOG] %s:%d: %s\n", __FILE__, __LINE__, __VA_ARGS__)
#define DRIFT_LOG(...) DriftLog("[LOG] %s:%d: %s\n", __FILE__, __LINE__, __VA_ARGS__)
#define DRIFT_ASSERT_HARD(_condition_, ...) if(!(_condition_)){DriftAssertHelper(#_condition_, __FILE__, __LINE__, DRIFT_ASSERT_HARD, __VA_ARGS__); DriftAbort();}
#define DRIFT_ABORT(...) {DriftLog("[Abort] %s:%d\n\tReason: %s\n", __FILE__, __LINE__, __VA_ARGS__); DriftAbort();}
#define DRIFT_NYI() {DriftLog("[Abort] %s:%d\n\tReason: Not yet implemented.\n", __FILE__, __LINE__, ""); DriftAbort();}

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

size_t DriftIOSize(DriftIOFunc* io_func, void* user_ptr);

bool DriftIOFileRead(const char* filename, DriftIOFunc* io_func, void* user_ptr);
void DriftIOFileWrite(const char* filename, DriftIOFunc* io_func, void* user_ptr);

typedef struct DriftMem DriftMem;
void DriftAssetsReset(void);
DriftData DriftAssetLoad(DriftMem* mem, const char* format, ...);

typedef struct {uint w, h; void* pixels;} DriftImage;
DriftImage DriftAssetLoadImage(DriftMem* mem, const char* format, ...);
void DriftImageFree(DriftMem* mem, DriftImage img);

uint DriftLog2Ceil(u64 n);
u64 DriftNextPOT(u64 n);
static inline bool DriftIsPOT(uint n){return n == DriftNextPOT(n);}

#define DRIFT_MIN(a, b) ({typeof(a) _a = a; typeof(b) _b = b; _a < _b ? _a : _b;})
#define DRIFT_MAX(a, b) ({typeof(a) _a = a; typeof(b) _b = b; _a > _b ? _a : _b;})

typedef struct {
	u64 rand, sum;
} DriftSelectionContext;

bool DriftSelectWeight(DriftSelectionContext* ctx, u64 weight);

#if DRIFT_DEBUG
void unit_test_util(void);
void unit_test_math(void);
void unit_test_entity(void);
void unit_test_map(void);
void unit_test_component(void);
void unit_test_rtree(void);
#endif
