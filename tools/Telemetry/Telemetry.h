#pragma once

#ifndef TTELEMETRY_ENABLED
#define TTELEMETRY_ENABLED 1
#endif // TTELEMETRY_ENABLED

#ifndef TTELEMETRY_LINK 
#define TTELEMETRY_LINK 1
#endif // TTELEMETRY_LINK

#define __TELEMETRY_UNIQUE_NAME_HELPER_0(_0, _1)  _0##_1
#define __TELEMETRY_UNIQUE_NAME_HELPER(_0, _1) __TELEMETRY_UNIQUE_NAME_HELPER_0(_0, _1)
#define __TELEMETRY_UNIQUE_NAME(name) __TELEMETRY_UNIQUE_NAME_HELPER(name, __LINE__)

#ifdef _MSC_VER

#define __TELEM_ARGS_AUGMENTER(...) unused, __VA_ARGS__
#define __TELEM_EXPAND(x) x
#define __TELEM_GET_ARG_COUNT_PRIVATE(_1_, _2_, _3_, _4_, _5_, _6_, _7_, _8_, _9_, _10_, _11_, _12_, _13_, _14_, _15_, _16_, _17_, _18_, _19_, _20_, _21_, _22_, _23_, _24_, _25_, _26_, _27_, _28_, _29_, _30_, _31_, _32_, _33_, _34_, _35_, _36, _37, _38, _39, _40, _41, _42, _43, _44, _45, _46, _47, _48, _49, _50, _51, _52, _53, _54, _55, _56, _57, _58, _59, _60, _61, _62, _63, _64, _65, _66, _67, _68, _69, _70, count, ...) count
#define __TELEM_EXPAND_ARGS_PRIVATE(...) __TELEM_EXPAND(__TELEM_GET_ARG_COUNT_PRIVATE(__VA_ARGS__, 69, 68, 67, 66, 65, 64, 63, 62, 61, 60, 59, 58, 57, 56, 55, 54, 53, 52, 51, 50, 49, 48, 47, 46, 45, 44, 43, 42, 41, 40, 39, 38, 37, 36, 35, 34, 33, 32, 31, 30, 29, 28, 27, 26, 25, 24, 23, 22, 21, 20, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0))

#define __TELEM_ARG_COUNT(...)  __TELEM_EXPAND_ARGS_PRIVATE(__TELEM_ARGS_AUGMENTER(__VA_ARGS__))

#define __TELEM_IMPORT              __declspec(dllimport)
#define __TELEM_EXPORT              __declspec(dllexport)
#define __TELEM_PRAGMA(pragma)      __pragma(pragma)
#define __TELEM_PACK_PUSH(val)      __TELEM_PRAGMA(pack(push, val))
#define __TELEM_PACK_POP            __TELEM_PRAGMA(pack(pop))

#define __TELEM_WIDEN_HELPER(str)   L##str
#define __TELEM_WIDEN(str)          __TELEM_WIDEN_HELPER(str)

#else 

#define __TELEM_GET_ARG_COUNT_PRIVATE(_0, _1_, _2_, _3_, _4_, _5_, _6_, _7_, _8_, _9_, _10_, _11_, _12_, _13_, _14_, _15_, _16_, _17_, _18_, _19_, _20_, _21_, _22_, _23_, _24_, _25_, _26_, _27_, _28_, _29_, _30_, _31_, _32_, _33_, _34_, _35_, _36, _37, _38, _39, _40, _41, _42, _43, _44, _45, _46, _47, _48, _49, _50, _51, _52, _53, _54, _55, _56, _57, _58, _59, _60, _61, _62, _63, _64, _65, _66, _67, _68, _69, _70, count, ...) count
#define __TELEM_ARG_COUNT(...) __TELEM_GET_ARG_COUNT_PRIVATE(0, __VA_ARGS__, 70, 69, 68, 67, 66, 65, 64, 63, 62, 61, 60, 59, 58, 57, 56, 55, 54, 53, 52, 51, 50, 49, 48, 47, 46, 45, 44, 43, 42, 41, 40, 39, 38, 37, 36, 35, 34, 33, 32, 31, 30, 29, 28, 27, 26, 25, 24, 23, 22, 21, 20, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0)

#define __TELEM_IMPORT
#define __TELEM_EXPORT
#define __TELEM_PRAGMA(pragma)      _Pragma(#pragma)
#define __TELEM_PACK_PUSH(val)      __TELEM_PRAGMA(pack(push, val))
#define __TELEM_PACK_POP            __TELEM_PRAGMA(pack(pop))

#define __TELEM_WIDEN_HELPER(str)   L##str
#define __TELEM_WIDEN(str)          __TELEM_WIDEN_HELPER(str)

#endif // _MSC_VER


#if defined(WIN32) && !defined(X_DLL)
#ifndef TELEM_LIB
#define TELEM_LIB
#endif
#endif

#ifndef __TELEMETRYLIB_EXPORT

#ifdef TELEM_LIB
#define __TELEMETRYLIB_EXPORT
#else
#ifdef TELEMETRY_LIB_EXPORT
#define __TELEMETRYLIB_EXPORT __TELEM_EXPORT
#else
#define __TELEMETRYLIB_EXPORT __TELEM_IMPORT
#endif // !TELEMETRY_LIB_EXPORT
#endif // TELEM_LIB

#endif // !__TELEMETRYLIB_EXPORT

#if defined(_WIN32) || defined(_WIN64)
#if _WIN64
#define __TELEM_64 1
#else
#define __TELEM_64 0
#endif

#define __TELEM_WIN32 1
#else
#define __TELEM_WIN32 0
#endif

#if __GNUC__
#if __x86_64__ || __ppc64__
#define __TELEM_64 1
#else
#define __TELEM_64 0
#endif
#endif

#define TELEM_LIB_NAME "engine_TelemetryLib"
#define TELEM_LIB_NAME_WIDE __TELEM_WIDEN("engine_TelemetryLib")

#define __TELEM_TYPES

typedef char tt_int8;
typedef short tt_int16;
typedef int tt_int32;
typedef long long tt_int64;

typedef unsigned char tt_uint8;
typedef unsigned short tt_uint16;
typedef unsigned int tt_uint32;
typedef unsigned long long tt_uint64;

typedef int tt_bool;

#if __TELEM_64

typedef long long tt_intptr;
typedef unsigned long long tt_uintptr;
typedef long long tt_ptrdiff;
typedef unsigned long long tt_size;

#else

typedef int tt_intptr;
typedef unsigned int tt_uintptr;
typedef int tt_ptrdiff;
typedef unsigned int tt_size;

#endif // __TELEM_64

static_assert(sizeof(tt_uintptr) == sizeof(void*), "Size mismatch");

typedef enum TtConnectionType
{
    TtConnectionTypeTcp,
    TtConnectionTypeFile
} TtConnectionType;

typedef enum TtError
{
    TtErrorOk,
    TtErrorError,
    TtErrorInvalidParam,
    TtErrorInvalidContex,
    TtErrorArenaTooSmall,
    TtErrorNetNotInit,

    TtErrorHandeshakeFail
} TtError;

typedef enum TtMsgFlag
{
    TtMsgFlagsSeverityMsg = 1,
    TtMsgFlagsSeverityWarning = 2,
    TtMsgFlagsSeverityError = 4,
    TtMsgFlagsSeverityMask = 0x7,
} TtMsgFlag;

typedef tt_uint32 TtMsgFlags;

typedef enum TtFlag
{
    TtFlagDropData = 1, // Used for profiling overhead, data is not sent to server.
} TtFlag;

typedef enum TtConnectionFlag
{
    // Runtime will stream missing PDB to server on demand.
    // not needed if you copy PDB to symbol server.
    TtConnectionFlagNone = 0,
    TtConnectionFlagStreamPDB = 1,
} TtConnectionFlag;

typedef tt_uint32 TtConnectionFlags;

typedef enum TtConnectionStatus
{
    Conencted,
    Disconnected
} TtConnectionStatus;

typedef enum TtStat
{
    TtStatNumStalls,
} TtStat;

typedef enum TtLockResult
{
    TtLockResultAcquired,
    TtLockResultFail
} TtLockResult;

typedef enum TtLockState
{
    TtLockStateLocked,
    TtLockStateReleased,
} TtLockState;

static inline const char* TtLockStateToString(enum TtLockState ls)
{
    switch (ls) {
        case TtLockStateLocked:
            return "Locked";
        case TtLockStateReleased:
            return "Released";
        default:
            return "<ukn>";
    }
}

typedef enum TtPlotType
{
    TtPlotTypeTime,
    TtPlotTypeTime_us,
    TtPlotTypeTime_clocks,
    TtPlotTypeTime_cycles,
    TtPlotTypeInteger,
    TtPlotTypePercentage_computed,
    TtPlotTypePercentage_direct,
    TtPlotTypeUntyped
} TtPlotType;


const tt_int32 TELEM_INTERNAL_THREAD_GROUP_ID = -1;

typedef tt_uintptr TraceContexHandle;
const TraceContexHandle INVALID_TRACE_CONTEX = 0;


// IO callbacks.
typedef tt_uintptr TtFileHandle;
const TtFileHandle TELEM_INVALID_HANDLE = 0;

typedef TtFileHandle(*FileOpenFunc)(void* pUserData, const char*);
typedef void(*FileCloseFunc)(void* pUserData, TtFileHandle);
typedef tt_int32(*FileWriteFunc)(void* pUserData, TtFileHandle, const void*, tt_int32);

typedef void(*LogFunction)(void* pUserData, TtMsgFlags type, const char* pMsgNullTerm, tt_int32 lenWithoutTerm);


#define __TELEM_CALLSTACK_MAX_FRAMES 15

typedef struct TtCallStack
{
#ifdef __cplusplus
    TtCallStack() {
        id = -1;
        num = 0;
    }
#endif

    tt_int32 id;
    tt_int32 num;
    void* frames[__TELEM_CALLSTACK_MAX_FRAMES];
} TtCallStack;


#define _TELEM_SOURCE_INFO __FILE__, __LINE__

#ifdef __cplusplus
extern "C"
{
#endif

#if TTELEMETRY_LINK
#define __TELEM_API_BLANK(func) 
#else
#define __TELEM_API_BLANK(func) func
#endif

#define __TELEM_API_VOID(name, ...) __TELEMETRYLIB_EXPORT void name(__VA_ARGS__); \
        __TELEM_API_BLANK(inline void __blank##name(__VA_ARGS__) {})

#define __TELEM_API_BOOL(name, ...) __TELEMETRYLIB_EXPORT tt_bool name(__VA_ARGS__); \
        __TELEM_API_BLANK(inline tt_bool __blank##name(__VA_ARGS__) { return true; })

#define __TELEM_API_INT(name, ...) __TELEMETRYLIB_EXPORT tt_int32 name(__VA_ARGS__); \
        __TELEM_API_BLANK(inline tt_int32 __blank##name(__VA_ARGS__) { return -1; })

#define __TELEM_API_UINT64(name, ...) __TELEMETRYLIB_EXPORT tt_uint64 name(__VA_ARGS__); \
        __TELEM_API_BLANK(inline tt_uint64 __blank##name(__VA_ARGS__) { return 0; })

#define __TELEM_API_F32(name, ...) __TELEMETRYLIB_EXPORT float name(__VA_ARGS__); \
        __TELEM_API_BLANK(inline float __blank##name(__VA_ARGS__) { return 0.f; })

#define __TELEM_API_ERR(name, ...) __TELEMETRYLIB_EXPORT TtError name(__VA_ARGS__); \
        __TELEM_API_BLANK(inline TtError __blank##name(__VA_ARGS__) { return Ok; })

#if __TELEM_WIN32

    __TELEM_PRAGMA(warning(push))
        __TELEM_PRAGMA(warning(disable: 4100)) // unused param (caused by the blank functions).

#endif

    __TELEM_API_BOOL(TelemInit);
    __TELEM_API_VOID(TelemShutDown);

    // Context
    __TELEM_API_ERR(TelemInitializeContext, TraceContexHandle* ctx, void* pArena, tt_size bufLen);
    __TELEM_API_VOID(TelemShutdownContext, TraceContexHandle ctx);

    __TELEM_API_VOID(TelemSetContextLogFunc, TraceContexHandle ctx, LogFunction func, void* pUserData);
    __TELEM_API_VOID(TelemSetIoFuncs, TraceContexHandle ctx, FileOpenFunc open, FileCloseFunc close, FileWriteFunc write, void* pUserData);

    __TELEM_API_ERR(TelemOpen, TraceContexHandle ctx, const char* pAppName, const char* pBuildInfo, const char* pServerAddress,
        TtConnectionType conType, tt_uint16 serverPort, tt_int32 timeoutMS, TtConnectionFlags flags);

    __TELEM_API_BOOL(TelemClose, TraceContexHandle ctx);

    __TELEM_API_VOID(TelemTick, TraceContexHandle ctx);
    __TELEM_API_VOID(TelemFlush, TraceContexHandle ctx);
    __TELEM_API_VOID(TelemUpdateSymbolData, TraceContexHandle ctx);
    __TELEM_API_VOID(TelemSyncSymbols, TraceContexHandle ctx);

    __TELEM_API_VOID(TelemPause, TraceContexHandle ctx, tt_bool pause);
    __TELEM_API_BOOL(TelemIsPaused, TraceContexHandle ctx);

    __TELEM_API_VOID(TelemSetFlag, TraceContexHandle ctx, TtFlag flag, tt_bool set);

    __TELEM_API_INT(TelemGetStatI, TraceContexHandle ctx, TtStat stat);

    __TELEM_API_UINT64(TelemFastTime);
    __TELEM_API_F32(TelemFastTimeToMs, TraceContexHandle ctx, tt_uint64 time);

    // Thread
    __TELEM_API_VOID(TelemSetThreadName, TraceContexHandle ctx, tt_uint32 threadID, const char* pFmtString, tt_int32 numArgs, ...);
    __TELEM_API_VOID(TelemSetThreadGroup, TraceContexHandle ctx, tt_uint32 threadID, tt_int32 groupID);
    __TELEM_API_VOID(TelemSetThreadGroupName, TraceContexHandle ctx, tt_int32 groupID, const char* pFmtString, tt_int32 numArgs, ...);
    __TELEM_API_VOID(TelemSetThreadGroupDefaultSort, TraceContexHandle ctx, tt_int32 groupID, tt_int32 sortVal);

    // Callstack
    __TELEM_API_INT(TelemGetCallStack, TraceContexHandle ctx, TtCallStack* stackOut);
    __TELEM_API_INT(TelemSendCallStack, TraceContexHandle ctx, const TtCallStack* pStack);
    __TELEM_API_INT(TelemSendCallStackSkip, TraceContexHandle ctx, const TtCallStack* pStack, tt_int32 numToSkip);

    // Zones
    __TELEM_API_VOID(TelemEnter, TraceContexHandle ctx, const char* pFile, tt_int32 line, const char* pFmtString, tt_int32 numArgs, ...);
    __TELEM_API_VOID(TelemEnterEx, TraceContexHandle ctx, const char* pFile, tt_int32 line, tt_uint64* matchIdOut, tt_uint64 minNanoSec, const char* pFmtString, tt_int32 numArgs, ...);
    __TELEM_API_VOID(TelemLeave, TraceContexHandle ctx);
    __TELEM_API_VOID(TelemLeaveEx, TraceContexHandle ctx, tt_uint64 matchId);

    // Lock util
    __TELEM_API_VOID(TelemSetLockName, TraceContexHandle ctx, const void* pPtr, const char* pFmtString, tt_int32 numArgs, ...);
    __TELEM_API_VOID(TelemTryLock, TraceContexHandle ctx, const char* pFile, tt_int32 line, const void* pPtr, const char* pFmtString, tt_int32 numArgs, ...);
    __TELEM_API_VOID(TelemTryLockEx, TraceContexHandle ctx, const char* pFile, tt_int32 line, tt_uint64* matchIdOut, tt_uint64 minNanoSec, const void* pPtr, const char* pFmtString, tt_int32 numArgs, ...);
    __TELEM_API_VOID(TelemEndTryLock, TraceContexHandle ctx, const void* pPtr, TtLockResult result);
    __TELEM_API_VOID(TelemEndTryLockEx, TraceContexHandle ctx, tt_uint64 matchId, const void* pPtr, TtLockResult result);
    __TELEM_API_VOID(TelemSetLockState, TraceContexHandle ctx, const char* pFile, tt_int32 line, const void* pPtr, TtLockState state);
    __TELEM_API_VOID(TelemSignalLockCount, TraceContexHandle ctx, const char* pFile, tt_int32 line, const void* pPtr, tt_int32 count);

    // Some allocation tracking.
    __TELEM_API_VOID(TelemAlloc, TraceContexHandle ctx, const char* pFile, tt_int32 line, void* pPtr, tt_size allocSize, const char* pFmtString, tt_int32 numArgs, ...);
    __TELEM_API_VOID(TelemFree, TraceContexHandle ctx, const char* pFile, tt_int32 line, void* pPtr);

    __TELEM_API_VOID(TelemPlotF32, TraceContexHandle ctx, TtPlotType type, float value, const char* pFmtString, tt_int32 numArgs, ...);
    __TELEM_API_VOID(TelemPlotF64, TraceContexHandle ctx, TtPlotType type, double value, const char* pFmtString, tt_int32 numArgs, ...);
    __TELEM_API_VOID(TelemPlotI32, TraceContexHandle ctx, TtPlotType type, tt_int32 value, const char* pFmtString, tt_int32 numArgs, ...);
    __TELEM_API_VOID(TelemPlotI64, TraceContexHandle ctx, TtPlotType type, tt_int64 value, const char* pFmtString, tt_int32 numArgs, ...);
    __TELEM_API_VOID(TelemPlotU32, TraceContexHandle ctx, TtPlotType type, tt_uint32 value, const char* pFmtString, tt_int32 numArgs, ...);
    __TELEM_API_VOID(TelemPlotU64, TraceContexHandle ctx, TtPlotType type, tt_uint64 value, const char* pFmtString, tt_int32 numArgs, ...);

    __TELEM_API_VOID(TelemMessage, TraceContexHandle ctx, TtMsgFlags flags, const char* pFmtString, tt_int32 numArgs, ...);


#if __TELEM_WIN32

    __TELEM_PRAGMA(warning(pop))

#endif

#ifdef __cplusplus
} // extern "C"
#endif // __cplusplus

#if TTELEMETRY_LINK
#define __TELEM_FUNC_NAME(name) name
#define TELEM_DYNAMIC_POINTERS 
#else
#define __TELEM_FUNC_NAME(name) __gTelemApi.p##name
#define TELEM_DYNAMIC_POINTERS TelemetryAPI __gTelemApi;

struct TelemetryAPI
{
#define __TELEM_RESOLVE(name) p##name= (decltype(name)*)::GetProcAddress(hLib_, # name); if(!p##name) { return false; }
#define __TELEM_SET_BLANK(name) p##name= __blank##name;
#define __TELEM_FUNC_PTR(name) decltype(name)* p##name;

    TelemetryAPI() {
        memset(this, 0, sizeof(*this));
        setBlank();
    }

    tt_bool loadModule()
    {
        hLib_ = ::LoadLibraryW(TELEM_LIB_NAME_WIDE);
        if (!hLib_) {
            return false;
        }

        __TELEM_RESOLVE(TelemInit);
        __TELEM_RESOLVE(TelemShutDown);
        __TELEM_RESOLVE(TelemInitializeContext);
        __TELEM_RESOLVE(TelemShutdownContext);
        __TELEM_RESOLVE(TelemSetContextLogFunc);
        __TELEM_RESOLVE(TelemSetIoFuncs);
        __TELEM_RESOLVE(TelemOpen);
        __TELEM_RESOLVE(TelemClose);
        __TELEM_RESOLVE(TelemTick);
        __TELEM_RESOLVE(TelemFlush);
        __TELEM_RESOLVE(TelemUpdateSymbolData);
        __TELEM_RESOLVE(TelemSyncSymbols);
        __TELEM_RESOLVE(TelemPause);
        __TELEM_RESOLVE(TelemIsPaused);
        __TELEM_RESOLVE(TelemSetFlag);
        __TELEM_RESOLVE(TelemGetStatI);
        __TELEM_RESOLVE(TelemFastTime);
        __TELEM_RESOLVE(TelemFastTimeToMs);
        __TELEM_RESOLVE(TelemSetThreadName);
        __TELEM_RESOLVE(TelemSetThreadGroup);
        __TELEM_RESOLVE(TelemSetThreadGroupName);
        __TELEM_RESOLVE(TelemSetThreadGroupDefaultSort);
        __TELEM_RESOLVE(TelemGetCallStack);
        __TELEM_RESOLVE(TelemSendCallStack);
        __TELEM_RESOLVE(TelemSendCallStackSkip);
        __TELEM_RESOLVE(TelemEnter);
        __TELEM_RESOLVE(TelemEnterEx);
        __TELEM_RESOLVE(TelemLeave);
        __TELEM_RESOLVE(TelemLeaveEx);
        __TELEM_RESOLVE(TelemSetLockName);
        __TELEM_RESOLVE(TelemTryLock);
        __TELEM_RESOLVE(TelemTryLockEx);
        __TELEM_RESOLVE(TelemEndTryLock);
        __TELEM_RESOLVE(TelemEndTryLockEx);
        __TELEM_RESOLVE(TelemSetLockState);
        __TELEM_RESOLVE(TelemSignalLockCount);
        __TELEM_RESOLVE(TelemAlloc);
        __TELEM_RESOLVE(TelemFree);
        __TELEM_RESOLVE(TelemPlotF32);
        __TELEM_RESOLVE(TelemPlotF64);
        __TELEM_RESOLVE(TelemPlotI32);
        __TELEM_RESOLVE(TelemPlotI64);
        __TELEM_RESOLVE(TelemPlotU32);
        __TELEM_RESOLVE(TelemPlotU64);
        __TELEM_RESOLVE(TelemMessage);
        return true;
    }

    void setBlank()
    {
        __TELEM_SET_BLANK(TelemInit);
        __TELEM_SET_BLANK(TelemShutDown);
        __TELEM_SET_BLANK(TelemInitializeContext);
        __TELEM_SET_BLANK(TelemShutdownContext);
        __TELEM_SET_BLANK(TelemSetContextLogFunc);
        __TELEM_SET_BLANK(TelemSetIoFuncs);
        __TELEM_SET_BLANK(TelemOpen);
        __TELEM_SET_BLANK(TelemClose);
        __TELEM_SET_BLANK(TelemTick);
        __TELEM_SET_BLANK(TelemFlush);
        __TELEM_SET_BLANK(TelemUpdateSymbolData);
        __TELEM_SET_BLANK(TelemSyncSymbols);
        __TELEM_SET_BLANK(TelemPause);
        __TELEM_SET_BLANK(TelemIsPaused);
        __TELEM_SET_BLANK(TelemSetFlag);
        __TELEM_SET_BLANK(TelemGetStatI);
        __TELEM_SET_BLANK(TelemFastTime);
        __TELEM_SET_BLANK(TelemFastTimeToMs);
        __TELEM_SET_BLANK(TelemSetThreadName);
        __TELEM_SET_BLANK(TelemSetThreadGroup);
        __TELEM_SET_BLANK(TelemSetThreadGroupName);
        __TELEM_SET_BLANK(TelemSetThreadGroupDefaultSort);
        __TELEM_SET_BLANK(TelemGetCallStack);
        __TELEM_SET_BLANK(TelemSendCallStack);
        __TELEM_SET_BLANK(TelemSendCallStackSkip);
        __TELEM_SET_BLANK(TelemEnter);
        __TELEM_SET_BLANK(TelemEnterEx);
        __TELEM_SET_BLANK(TelemLeave);
        __TELEM_SET_BLANK(TelemLeaveEx);
        __TELEM_SET_BLANK(TelemSetLockName);
        __TELEM_SET_BLANK(TelemTryLock);
        __TELEM_SET_BLANK(TelemTryLockEx);
        __TELEM_SET_BLANK(TelemEndTryLock);
        __TELEM_SET_BLANK(TelemEndTryLockEx);
        __TELEM_SET_BLANK(TelemSetLockState);
        __TELEM_SET_BLANK(TelemSignalLockCount);
        __TELEM_SET_BLANK(TelemAlloc);
        __TELEM_SET_BLANK(TelemFree);
        __TELEM_SET_BLANK(TelemPlotF32);
        __TELEM_SET_BLANK(TelemPlotF64);
        __TELEM_SET_BLANK(TelemPlotI32);
        __TELEM_SET_BLANK(TelemPlotI64);
        __TELEM_SET_BLANK(TelemPlotU32);
        __TELEM_SET_BLANK(TelemPlotU64);
        __TELEM_SET_BLANK(TelemMessage);
    }

    void unLoad()
    {
        if (hLib_) {
            ::FreeLibrary(hLib_);
        }

        memset(this, 0, sizeof(*this));
    }

    __TELEM_FUNC_PTR(TelemInit);
    __TELEM_FUNC_PTR(TelemShutDown);
    __TELEM_FUNC_PTR(TelemInitializeContext);
    __TELEM_FUNC_PTR(TelemShutdownContext);
    __TELEM_FUNC_PTR(TelemSetContextLogFunc);
    __TELEM_FUNC_PTR(TelemSetIoFuncs);
    __TELEM_FUNC_PTR(TelemOpen);
    __TELEM_FUNC_PTR(TelemClose);
    __TELEM_FUNC_PTR(TelemTick);
    __TELEM_FUNC_PTR(TelemFlush);
    __TELEM_FUNC_PTR(TelemUpdateSymbolData);
    __TELEM_FUNC_PTR(TelemSyncSymbols);
    __TELEM_FUNC_PTR(TelemPause);
    __TELEM_FUNC_PTR(TelemIsPaused);
    __TELEM_FUNC_PTR(TelemSetFlag);
    __TELEM_FUNC_PTR(TelemGetStatI);
    __TELEM_FUNC_PTR(TelemFastTime);
    __TELEM_FUNC_PTR(TelemFastTimeToMs);
    __TELEM_FUNC_PTR(TelemSetThreadName);
    __TELEM_FUNC_PTR(TelemSetThreadGroup);
    __TELEM_FUNC_PTR(TelemSetThreadGroupName);
    __TELEM_FUNC_PTR(TelemSetThreadGroupDefaultSort);
    __TELEM_FUNC_PTR(TelemGetCallStack);
    __TELEM_FUNC_PTR(TelemSendCallStack);
    __TELEM_FUNC_PTR(TelemSendCallStackSkip);
    __TELEM_FUNC_PTR(TelemEnter);
    __TELEM_FUNC_PTR(TelemEnterEx);
    __TELEM_FUNC_PTR(TelemLeave);
    __TELEM_FUNC_PTR(TelemLeaveEx);
    __TELEM_FUNC_PTR(TelemSetLockName);
    __TELEM_FUNC_PTR(TelemTryLock);
    __TELEM_FUNC_PTR(TelemTryLockEx);
    __TELEM_FUNC_PTR(TelemEndTryLock);
    __TELEM_FUNC_PTR(TelemEndTryLockEx);
    __TELEM_FUNC_PTR(TelemSetLockState);
    __TELEM_FUNC_PTR(TelemSignalLockCount);
    __TELEM_FUNC_PTR(TelemAlloc);
    __TELEM_FUNC_PTR(TelemFree);
    __TELEM_FUNC_PTR(TelemPlotF32);
    __TELEM_FUNC_PTR(TelemPlotF64);
    __TELEM_FUNC_PTR(TelemPlotI32);
    __TELEM_FUNC_PTR(TelemPlotI64);
    __TELEM_FUNC_PTR(TelemPlotU32);
    __TELEM_FUNC_PTR(TelemPlotU64);
    __TELEM_FUNC_PTR(TelemMessage);

private:
    HMODULE hLib_;
};

// if you have multiple dll's this will be different.
// so you would have to call init in each dll.
// but also means you can conditionally enable telemetry for various modules.
// it's not safe to resolve functions during a zone for the same module.
extern TelemetryAPI __gTelemApi;

#endif // TTELEMETRY_LINK

#ifdef __cplusplus

#include  <utility>

namespace telem
{
    static const tt_int32 DEFAULT_PORT = 8001;
    static const char* DEFAULT_PORT_STR = "8001";

    using ContexHandle = TraceContexHandle;
    using ConnectionType = TtConnectionType;
    using LockResult = TtLockResult;
    using LockState = TtLockState;
    using PlotType = TtPlotType;
    using Error = TtError;

    struct ScopedZone
    {
        inline ScopedZone(TraceContexHandle ctx, const char* pFile, tt_int32 line, const char* pFormat) :
            ctx_(ctx)
        {
            __TELEM_FUNC_NAME(TelemEnter)(ctx, pFile, line, pFormat, 0);
        }

        template<typename ... Args>
        inline ScopedZone(TraceContexHandle ctx, const char* pFile, tt_int32 line, const char* pFormat, Args&& ... args) :
            ctx_(ctx)
        {
            const tt_int32 num = sizeof...(Args);
            __TELEM_FUNC_NAME(TelemEnter)(ctx, pFile, line, pFormat, num, std::forward<Args>(args) ...);
        }

        inline ~ScopedZone() {
            __TELEM_FUNC_NAME(TelemLeave)(ctx_);
        }

    private:
        TraceContexHandle ctx_;
    };

    struct ScopedZoneFilterd
    {
        inline ScopedZoneFilterd(TraceContexHandle ctx, const char* pFile, tt_int32 line, tt_uint64 minNanoSec, const char* pFormat) :
            ctx_(ctx)
        {
            __TELEM_FUNC_NAME(TelemEnterEx)(ctx, pFile, line, &matchId_, minNanoSec, pFormat, 0);
        }

        template<typename ... Args>
        inline ScopedZoneFilterd(TraceContexHandle ctx, const char* pFile, tt_int32 line, tt_uint64 minNanoSec, const char* pFormat, Args&& ... args) :
            ctx_(ctx)
        {
            const tt_int32 num = sizeof...(Args);
            __TELEM_FUNC_NAME(TelemEnterEx)(ctx, pFile, line, matchId_, minNanoSec, pFormat, num, std::forward<Args>(args) ...);
        }

        inline ~ScopedZoneFilterd() {
            __TELEM_FUNC_NAME(TelemLeaveEx)(ctx_, matchId_);
        }

    private:
        TraceContexHandle ctx_;
        tt_uint64 matchId_;
    };

    class SpikeDetector
    {
    public:
        SpikeDetector(TraceContexHandle ctx, const char* pMsg, float thresholdMS = 5) :
            ctx_(ctx),
            begin_(__TELEM_FUNC_NAME(TelemFastTime)()),
            pMsg_(pMsg),
            msThreshold_(thresholdMS)
        {
        }

        ~SpikeDetector()
        {
            float time = __TELEM_FUNC_NAME(TelemFastTimeToMs)(ctx_, __TELEM_FUNC_NAME(TelemFastTime)() - begin_);
            if (time >= msThreshold_) {
                __TELEM_FUNC_NAME(TelemMessage)(ctx_, TtMsgFlagsSeverityError, "(spike)%s %.2fms", 2, pMsg_, time);
            }
        }

    private:
        TraceContexHandle ctx_;
        tt_uint64 begin_;
        const char* pMsg_;
        float msThreshold_;
    };


} // namespace telem

#endif // __cplusplus

#if TTELEMETRY_ENABLED

#if TTELEMETRY_LINK
#define ttLoadLibary() true
#else
#define ttLoadLibary() __gTelemApi.loadModule()
#endif // TTELEMETRY_LINK

#ifdef __cplusplus
#define ttZone(ctx, pFmtString, ...) telem::ScopedZone __TELEMETRY_UNIQUE_NAME(scopedzone_)(ctx, _TELEM_SOURCE_INFO, pFmtString, ## __VA_ARGS__)
#define ttZoneFilterd(ctx, minNanoSec, pFmtString, ...) telem::ScopedZoneFilterd __TELEMETRY_UNIQUE_NAME(scopedzone_)(ctx, _TELEM_SOURCE_INFO, minNanoSec, pFmtString, ## __VA_ARGS__)
#endif // __cplusplus

#define ttZoneFunction(ctx) ttZone(ctx, __FUNCTION__)
#define ttZoneFunctionFilterd(ctx, minNanoSec) ttZoneFilterd(ctx, minNanoSec, __FUNCTION__)

#define ttInit() __TELEM_FUNC_NAME(TelemInit)()
#define ttShutDown() __TELEM_FUNC_NAME(TelemShutDown)()

// Context
#define ttInitializeContext(out, pBuf, bufLen) __TELEM_FUNC_NAME(TelemInitializeContext)(out, pBuf, bufLen)
#define ttShutdownContext(ctx) __TELEM_FUNC_NAME(TelemShutdownContext)(ctx)

#define ttSetContextLogFunc(ctx, func, pUserData) __TELEM_FUNC_NAME(TelemSetContextLogFunc)(ctx, func, pUserData)
#define ttSetContextIoFuncs(ctx, open, close, write, pUserData) __TELEM_FUNC_NAME(TelemSetIoFuncs)(ctx, open, close, write, pUserData)

#define ttOpen(ctx, pAppName, pBuildInfo, pServerAddress, conType, serverPort, timeoutMS, flags) \
    __TELEM_FUNC_NAME(TelemOpen)(ctx, pAppName, pBuildInfo, pServerAddress, conType, serverPort, timeoutMS, flags)

#define ttClose(ctx) __TELEM_FUNC_NAME(TelemClose)(ctx)

#define ttTick(ctx) __TELEM_FUNC_NAME(TelemTick)(ctx)
#define ttFlush(ctx) __TELEM_FUNC_NAME(TelemFlush)(ctx)
#define ttUpdateSymbolData(ctx) __TELEM_FUNC_NAME(TelemUpdateSymbolData)(ctx)
#define ttSyncSymbols(ctx) __TELEM_FUNC_NAME(TelemSyncSymbols)(ctx)

#define ttPause(ctx, pause) __TELEM_FUNC_NAME(TelemPause)(ctx, pause)
#define ttIsPaused(ctx) __TELEM_FUNC_NAME(TelemIsPaused)(ctx)

#define ttSetFlag(ctx, flag, set) __TELEM_FUNC_NAME(TelemSetFlag)(ctx, flag, set)

#define ttGetStati(ctx, stat) __TELEM_FUNC_NAME(TelemGetStatI)(ctx, stat)

#define ttFastTime() __TELEM_FUNC_NAME(TelemFastTime)()
#define ttFastTimeToMs(ctx, time) __TELEM_FUNC_NAME(TelemFastTimeToMs)(ctx, time)

// Thread
#define ttSetThreadName(ctx, threadID, pFmtString, ...) __TELEM_FUNC_NAME(TelemSetThreadName)(ctx, threadID, pFmtString, __TELEM_ARG_COUNT(__VA_ARGS__), ## __VA_ARGS__)
#define ttSetThreadGroup(ctx, threadID, groupID) __TELEM_FUNC_NAME(TelemSetThreadGroup)(ctx, threadID, groupID)
#define ttSetThreadGroupName(ctx, groupID, pFmtString, ...) __TELEM_FUNC_NAME(TelemSetThreadGroupName)(ctx, groupID, pFmtString, __TELEM_ARG_COUNT(__VA_ARGS__), ## __VA_ARGS__)
#define ttSetThreadGroupDefaultSort(ctx, groupID, idx) __TELEM_FUNC_NAME(TelemSetThreadGroupDefaultSort)(ctx, groupID, idx)

// Callstack
#define ttGetCallStack(ctx, stackOut) __TELEM_FUNC_NAME(TelemGetCallStack)(ctx, &stackOut)
#define ttSendCallStack(ctx, pStack) __TELEM_FUNC_NAME(TelemSendCallStack)(ctx, pStack)
#define ttSendCallStackSkip(ctx, pStack, numToSkip) __TELEM_FUNC_NAME(TelemSendCallStackSkip)(ctx, pStack, numToSkip)

// Zones
#define ttEnter(ctx, pFmtString, ...) __TELEM_FUNC_NAME(TelemEnter)(ctx, _TELEM_SOURCE_INFO, pFmtString, __TELEM_ARG_COUNT(__VA_ARGS__), ## __VA_ARGS__)
#define ttEnterEx(ctx, matchIdOut, minNanoSec, pFmtString, ...) __TELEM_FUNC_NAME(TelemEnterEx)(ctx, _TELEM_SOURCE_INFO, &matchIdOut, minNanoSec, pFmtString, __TELEM_ARG_COUNT(__VA_ARGS__), ## __VA_ARGS__)
#define ttLeave(ctx) __TELEM_FUNC_NAME(TelemLeave)(ctx)
#define ttLeaveEx(ctx, matchId) __TELEM_FUNC_NAME(TelemLeaveEx)(ctx, matchId)


// Lock util
#define ttSetLockName(ctx, pPtr, pFmtString, ...) __TELEM_FUNC_NAME(TelemSetLockName)(ctx, pPtr, pFmtString, __TELEM_ARG_COUNT(__VA_ARGS__), ## __VA_ARGS__)
#define ttTryLock(ctx, pPtr, pFmtString, ...) __TELEM_FUNC_NAME(TelemTryLock)(ctx, _TELEM_SOURCE_INFO, pPtr, pFmtString, __TELEM_ARG_COUNT(__VA_ARGS__), ## __VA_ARGS__)
#define ttTryLockEx(ctx, matchIdOut, minNanoSec, pPtr, pFmtString, ...) __TELEM_FUNC_NAME(TelemTryLockEx)(ctx, _TELEM_SOURCE_INFO, matchIdOut, minNanoSec, pPtr, pFmtString, __TELEM_ARG_COUNT(__VA_ARGS__), ## __VA_ARGS__)
#define ttEndTryLock(ctx, pPtr, result) __TELEM_FUNC_NAME(TelemEndTryLock)(ctx, pPtr, result)
#define ttEndTryLockEx(ctx, matchIdOut, pPtr, result) __TELEM_FUNC_NAME(TelemEndTryLockEx)(ctx, matchIdOut, pPtr, result)
#define ttSetLockState(ctx, pPtr, state) __TELEM_FUNC_NAME(TelemSetLockState)(ctx, _TELEM_SOURCE_INFO, pPtr, state)
#define ttSignalLockCount(ctx, pPtr, count) __TELEM_FUNC_NAME(TelemSignalLockCount)(ctx, _TELEM_SOURCE_INFO, pPtr, count)

// Some allocation tracking.
#define ttAlloc(ctx, pPtr, size, pFmtString, ...) __TELEM_FUNC_NAME(TelemAlloc)(ctx, _TELEM_SOURCE_INFO, pPtr, size, pFmtString, __TELEM_ARG_COUNT(__VA_ARGS__), ## __VA_ARGS__)
#define ttFree(ctx, pPtr) __TELEM_FUNC_NAME(TelemFree)(ctx, _TELEM_SOURCE_INFO, pPtr)

// Plots
#define ttPlot(ctx, type, value, pFmtString, ...)    ttPlotF32(ctx, type, value, pFmtString, ## __VA_ARGS__);
#define ttPlotF32(ctx, type, value, pFmtString, ...)  __TELEM_FUNC_NAME(TelemPlotF32)(ctx, type, value, pFmtString, __TELEM_ARG_COUNT(__VA_ARGS__), ## __VA_ARGS__)
#define ttPlotF64(ctx, type, value, pFmtString, ...)  __TELEM_FUNC_NAME(TelemPlotF64)(ctx, type, value, pFmtString, __TELEM_ARG_COUNT(__VA_ARGS__), ## __VA_ARGS__)
#define ttPlotI32(ctx, type, value, pFmtString, ...)  __TELEM_FUNC_NAME(TelemPlotI32)(ctx, type, value, pFmtString, __TELEM_ARG_COUNT(__VA_ARGS__), ## __VA_ARGS__)
#define ttPlotU32(ctx, type, value, pFmtString, ...)  __TELEM_FUNC_NAME(TelemPlotU32)(ctx, type, value, pFmtString, __TELEM_ARG_COUNT(__VA_ARGS__), ## __VA_ARGS__)
#define ttPlotI64(ctx, type, value, pFmtString, ...)  __TELEM_FUNC_NAME(TelemPlotI64)(ctx, type, value, pFmtString, __TELEM_ARG_COUNT(__VA_ARGS__), ## __VA_ARGS__)
#define ttPlotU64(ctx, type, value, pFmtString, ...)  __TELEM_FUNC_NAME(TelemPlotU64)(ctx, type, value, pFmtString, __TELEM_ARG_COUNT(__VA_ARGS__), ## __VA_ARGS__)

// Messages
#define ttMessage(ctx, type, pFmtString, ...)  __TELEM_FUNC_NAME(TelemMessage)(ctx, type, pFmtString, __TELEM_ARG_COUNT(__VA_ARGS__), ## __VA_ARGS__)
#define ttLog(ctx, pFmtString, ...) ttMessage(ctx, TtMsgFlagsSeverityMsg, pFmtString, ## __VA_ARGS__)
#define ttWarning(ctx, pFmtString, ...) ttMessage(ctx, TtMsgFlagsSeverityWarning, pFmtString, ## __VA_ARGS__)
#define ttError(ctx, pFmtString, ...) ttMessage(ctx, TtMsgFlagsSeverityError, pFmtString, ## __VA_ARGS__)


#else // TTELEMETRY_ENABLED

#define ttLoadLibary() true

#define ttZone(...)
#define ttZoneFilterd(...)

#define ttInit() true
#define ttShutDown() 

#define ttSetContextLogFunc(...)
#define ttSetContextIoFuncs(...)

// Context
#define ttInitializeContext(...)
#define ttShutdownContext(...)

#define ttOpen(...)

#define ttClose(...)

#define ttTick(...)
#define ttFlush(...)
#define ttUpdateSymbolData(...)
#define ttSyncSymbols(...)

#define ttPause(...)
#define ttIsPaused(...)

#define ttSetFlag(...)

#define ttGetStati(...)

// Thread
#define ttSetThreadName(...)
#define ttSetThreadGroup(...)
#define ttSetThreadGroupName(...)
#define ttSetThreadGroupDefaultSort(...)

#define ttGetCallStack(...)
#define ttSendCallStack(...)
#define ttSendCallStackSkip(...)

// Zones
#define ttEnter(...);
#define ttEnterEx(...);
#define ttLeave(...);
#define ttLeaveEx(...);


// Lock util
#define ttSetLockName(...);
#define ttTryLock(...);
#define ttTryLockEx(...);
#define ttEndTryLock(...);
#define ttEndTryLockEx(...);
#define ttSetLockState(...);
#define ttSignalLockCount(...);

#define ttAlloc(...);
#define ttFree(...);

#define ttPlot(...);
#define ttPlotF32(...);
#define ttPlotF64(...);
#define ttPlotI32(...);
#define ttPlotU32(...);
#define ttPlotI64(...);
#define ttPlotU64(...);

#define ttMessage(...);
#define ttLog(...);
#define ttWarning(...);
#define ttError(...);

#endif // TTELEMETRY_ENABLED
