#include <EngineCommon.h>
#include "Thread.h"

#if !defined(__midl) && !defined(GENUTIL) && !defined(_GENIA64_) && defined(_IA64_)
X_INTRINSIC(__yield)
#else
X_INTRINSIC(_mm_pause)
#endif

X_NAMESPACE_BEGIN(core)

namespace
{
    const DWORD MS_VC_EXCEPTION = 0x406D1388;

    using pSetThreadDescription = HRESULT(WINAPI *)(HANDLE hThread, PCWSTR lpThreadDescription);

    X_PACK_PUSH(8)
    struct THREADNAME_INFO
    {
        DWORD dwType;     // Must be 0x1000.
        LPCSTR szName;    // Pointer to name (in user addr space).
        DWORD dwThreadID; // Thread ID (-1=caller thread).
        DWORD dwFlags;    // Reserved for future use, must be zero.
    };
    X_PACK_POP;

    void setThreadName(HANDLE hThread, DWORD dwThreadID, const char* pThreadName)
    {
        THREADNAME_INFO info;
        info.dwType = 0x1000;
        info.szName = pThreadName;
        info.dwThreadID = dwThreadID;
        info.dwFlags = 0;

        __try {
            RaiseException(MS_VC_EXCEPTION, 0, sizeof(info) / sizeof(ULONG_PTR), (ULONG_PTR*)&info);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
        }

        if (hThread != NULL) {
            HMODULE hMod = ::GetModuleHandleW(L"kernel32.dll");

            auto pFunc = (pSetThreadDescription)::GetProcAddress(hMod, "SetThreadDescription");
            if (pFunc) {
                core::StackStringW256 name(pThreadName);

                auto hr = pFunc(hThread, name.c_str());
                if (FAILED(hr)) {
                    X_ERROR("Thread", "Failed to SetThreadDescription. Err: %" PRIi32, hr);
                }
            }
        }
    }

} // namespace

Thread::Thread(void) :
    handle_(NULL),
    id_(0),
    function_(nullptr),
    state_(State::NOT_CREATED)
{
}

Thread::~Thread(void)
{
}

void Thread::create(const char* pName, uint32_t stackSize)
{
    name_.set(pName);

    if (!createThreadInternal(stackSize, (LPTHREAD_START_ROUTINE)ThreadFunction_)) {
        lastError::Description Dsc;
        X_ERROR("Thread", "failed to create thread. Erorr: %s", lastError::ToString(Dsc));
    }
    else {
        state_ = State::READY;
    }
}

void Thread::destroy(void)
{
    stop();
    join();
}

void Thread::start(Function::Pointer function)
{
    function_ = function;
    state_ = State::RUNNING;

    if (ResumeThread(handle_) == (DWORD)-1) {
        lastError::Description Dsc;
        X_ERROR("Thread", "failed to start thread. Erorr: %s", lastError::ToString(Dsc));
    }

    setThreadName(handle_, getID(), name_.c_str());
}

void Thread::stop(void)
{
    if (state_ != State::READY) {
        state_ = State::STOPPING;
    }
}

void Thread::join(void)
{
    if (state_ != State::READY && handle_ != NULL) {
        if (WaitForSingleObject(handle_, INFINITE) == WAIT_FAILED) {
            lastError::Description Dsc;
            X_ERROR("Thread", "thread join failed. Erorr: %s", lastError::ToString(Dsc));
        }
    }
    handle_ = NULL;
    state_ = State::FINISHED;
    id_ = 0;
}

bool Thread::shouldRun(void) const volatile
{
    return state_ == State::RUNNING;
}

bool Thread::hasFinished(void) const volatile
{
    return state_ == State::FINISHED;
}

bool Thread::setThreadAffinity(const AffinityFlags flags)
{
    DWORD_PTR mask = flags.ToInt();

    if (SetThreadAffinityMask(handle_, mask) == 0) {
        lastError::Description Dsc;
        X_ERROR("Thread", "Failed to set thread affinity. Error: %s", lastError::ToString(Dsc));
        return false;
    }

    return true;
}

void Thread::setFPE(FPE::Enum fpe)
{
    setFPE(getID(), fpe);
}

void Thread::cancelSynchronousIo(void)
{
    if (!::CancelSynchronousIo(handle_)) {
        lastError::Description Dsc;
        X_ERROR("Thread", "Failed to cancel sync io. Error: %s", lastError::ToString(Dsc));
    }
}

bool Thread::sleepAlertable(uint32_t milliSeconds)
{
    DWORD res = ::SleepEx(milliSeconds, TRUE);
    if (res == WAIT_IO_COMPLETION) {
        return true;
    }
    else if (res != 0) {
        lastError::Description Dsc;
        X_ERROR("Thread", "SleepEx. Error: %s", lastError::ToString(res, Dsc));
    }

    return false;
}

void Thread::join(uint32_t threadId)
{
    HANDLE hThread = ::OpenThread(THREAD_ALL_ACCESS, FALSE, threadId);
    if (hThread == NULL) {
        lastError::Description Dsc;
        X_ERROR("Thread", "Failed to get thread handle for id: % " PRIu32 ". Error: %s", threadId, lastError::ToString(Dsc));
        return;
    }

    if (WaitForSingleObject(hThread, INFINITE) == WAIT_FAILED) {
        lastError::Description Dsc;
        X_ERROR("Thread", "thread join failed. Erorr: %s", lastError::ToString(Dsc));
    }
}

void Thread::setName(uint32_t threadId, const char* name)
{
    HANDLE handle = OpenThread(
        THREAD_SET_LIMITED_INFORMATION,
        FALSE,
        threadId
    );

    setThreadName(handle, threadId, name);

    ::CloseHandle(handle);
}

Thread::Priority::Enum Thread::getPriority(void)
{
    int32_t pri = ::GetThreadPriority(::GetCurrentThread());
    if (pri == THREAD_PRIORITY_ERROR_RETURN) {
        lastError::Description Dsc;
        X_ERROR("Thread", "Failed to get thread priority for id: % " PRIu32 ". Error: %s",
            getCurrentID(), lastError::ToString(Dsc));
        return Priority::NORMAL;
    }

    switch (pri) {
        case THREAD_PRIORITY_LOWEST:
            return Priority::LOWEST;
        case THREAD_PRIORITY_BELOW_NORMAL:
            return Priority::BELOW_NORMAL;
        case THREAD_PRIORITY_NORMAL:
            return Priority::NORMAL;
        case THREAD_PRIORITY_ABOVE_NORMAL:
            return Priority::ABOVE_NORMAL;
        case THREAD_PRIORITY_HIGHEST:
            return Priority::HIGHEST;
        case THREAD_PRIORITY_TIME_CRITICAL:
            return Priority::REALTIME;
        case THREAD_PRIORITY_IDLE:
            return Priority::IDLE;

        default:
            X_ASSERT_UNREACHABLE();
            break;
    }

    return Priority::NORMAL;
}

bool Thread::setPriority(Priority::Enum priority)
{
    int pri = THREAD_PRIORITY_NORMAL;

    switch (priority) {
        case Priority::LOWEST:
            pri = THREAD_PRIORITY_LOWEST;
            break;
        case Priority::BELOW_NORMAL:
            pri = THREAD_PRIORITY_BELOW_NORMAL;
            break;
        case Priority::NORMAL:
            pri = THREAD_PRIORITY_NORMAL;
            break;
        case Priority::ABOVE_NORMAL:
            pri = THREAD_PRIORITY_ABOVE_NORMAL;
            break;
        case Priority::HIGHEST:
            pri = THREAD_PRIORITY_HIGHEST;
            break;
        case Priority::REALTIME:
            pri = THREAD_PRIORITY_TIME_CRITICAL;
            break;
        case Priority::IDLE:
            pri = THREAD_PRIORITY_IDLE;
            break;

        default:
            X_ASSERT_UNREACHABLE();
            break;
    }

    if (!SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL)) {
        lastError::Description Dsc;
        X_ERROR("Thread", "Failed to set thread priority for id: % " PRIu32 ". Error: %s",
            getCurrentID(), lastError::ToString(Dsc));
        return false;
    }

    return true;
}

void Thread::setFPE(uint32_t threadId, FPE::Enum fpe)
{
    // Enable:
    // - _EM_ZERODIVIDE
    // - _EM_INVALID
    //
    // Disable:
    // - _EM_DENORMAL
    // - _EM_OVERFLOW
    // - _EM_UNDERFLOW
    // - _EM_INEXACT
    const uint32_t BASIC_DISABLE = _EM_INEXACT | _EM_DENORMAL | _EM_UNDERFLOW | _EM_OVERFLOW;
    const uint32_t BASIC_MM_DISABLE = _MM_MASK_DENORM | _MM_MASK_INEXACT | _MM_MASK_UNDERFLOW | _MM_MASK_OVERFLOW;

    // Enable:
    // - _EM_ZERODIVIDE
    // - _EM_INVALID
    // - _EM_UNDERFLOW
    // - _EM_OVERFLOW
    //
    // Disable:
    // - _EM_INEXACT
    // - _EM_DENORMAL
    const uint32_t ALL_DISABLE = _EM_INEXACT | _EM_DENORMAL;
    const uint32_t ALL_MM_DISABLE = _MM_MASK_INEXACT | _MM_MASK_DENORM;

    if (threadId == 0 || threadId == getCurrentID()) {
        uint32_t current = 0;

        _controlfp_s(&current, _DN_FLUSH, _MCW_DN);
        _mm_setcsr(_mm_getcsr() | _MM_FLUSH_ZERO_ON);

        if (fpe == FPE::NONE) {
            // mask all floating exceptions off.
            _controlfp_s(&current, _MCW_EM, _MCW_EM);
            _mm_setcsr(_mm_getcsr() | _MM_MASK_MASK);
        }
        else {
            // Clear pending exceptions
            _fpreset();

            if (fpe == FPE::BASIC) {
                _controlfp_s(&current, BASIC_DISABLE, _MCW_EM);
                _mm_setcsr((_mm_getcsr() & ~_MM_MASK_MASK) | BASIC_MM_DISABLE);
            }

            if (fpe == FPE::ALL) {
                _controlfp_s(&current, ALL_DISABLE, _MCW_EM);
                _mm_setcsr((_mm_getcsr() & ~_MM_MASK_MASK) | ALL_MM_DISABLE);
            }
        }
        return;
    }

    HANDLE hThread = OpenThread(THREAD_ALL_ACCESS, true, threadId);
    if (hThread == 0) {
        core::lastError::Description Dsc;
        X_WARNING("Thread", "Unable to open thread: %p Err: %s", hThread, core::lastError::ToString(Dsc));
        return;
    }

    SuspendThread(hThread);

    CONTEXT ctx;
    core::zero_object(ctx);
    ctx.ContextFlags = CONTEXT_ALL;
    if (GetThreadContext(hThread, &ctx) == 0) {
        core::lastError::Description Dsc;
        X_WARNING("Thread", "Failed to get thread contex: %p Err: %s", hThread, core::lastError::ToString(Dsc));
        ResumeThread(hThread);
        CloseHandle(hThread);
        return;
    }

#if X_64
    typedef WORD ControlType;

    DWORD& floatMxCsr = ctx.MxCsr;                           // Hold FPE Mask and Status for MMX (SSE) floating point registers
    ControlType& floatControlWord = ctx.FltSave.ControlWord; // Hold FPE Mask for floating point registers
    ControlType& floatStatuslWord = ctx.FltSave.StatusWord;  // Holds FPE Status for floating point registers

#else
    typedef DWORD ControlType;

    DWORD& floatMxCsr = *reinterpret_cast<DWORD*>(&ctx.ExtendedRegisters[24]);
    ControlType& floatControlWord = ctx.FloatSave.ControlWord; // Hold FPE Mask for floating point registers
    ControlType& floatStatuslWord = ctx.FloatSave.StatusWord;  // Holds FPE Status for floating point registers

#endif

    // Set flush mode to zero mode
    floatControlWord = static_cast<ControlType>((floatControlWord & ~_MCW_DN) | _DN_FLUSH);
    floatMxCsr = static_cast<ControlType>((floatMxCsr & ~_MM_FLUSH_ZERO_MASK) | (_MM_FLUSH_ZERO_ON));

    // Reset FPE bits
    floatControlWord = static_cast<ControlType>(floatControlWord | _MCW_EM);
    floatMxCsr = floatMxCsr | _MM_MASK_MASK;

    // Clear pending exceptions
    floatStatuslWord = floatStatuslWord & ~(_SW_INEXACT | _SW_UNDERFLOW | _SW_OVERFLOW | _SW_ZERODIVIDE | _SW_INVALID | _SW_DENORMAL);
    floatMxCsr = floatMxCsr & ~(_MM_EXCEPT_INEXACT | _MM_EXCEPT_UNDERFLOW | _MM_EXCEPT_OVERFLOW | _MM_EXCEPT_DIV_ZERO | _MM_EXCEPT_INVALID | _MM_EXCEPT_DENORM);

    if (fpe == FPE::BASIC) {
        floatControlWord = static_cast<ControlType>((floatControlWord & ~_MCW_EM) | BASIC_DISABLE);
        floatMxCsr = (floatMxCsr & ~_MM_MASK_MASK) | BASIC_MM_DISABLE;
    }

    if (fpe == FPE::ALL) {
        floatControlWord = static_cast<ControlType>((floatControlWord & ~_MCW_EM) | ALL_DISABLE);
        floatMxCsr = (floatMxCsr & ~_MM_MASK_MASK) | ALL_MM_DISABLE;
    }

    ctx.ContextFlags = CONTEXT_ALL;
    if (SetThreadContext(hThread, &ctx) == 0) {
        core::lastError::Description Dsc;
        X_WARNING("Thread", "Failed to set thread contex: %p Err: %s", hThread, core::lastError::ToString(Dsc));
        // fall through and resume.
    }

    ResumeThread(hThread);
    CloseHandle(hThread);
}

void Thread::setName(const char* pName)
{
    setThreadName(handle_, id_, pName);
}

bool Thread::createThreadInternal(uint32_t stackSize, LPTHREAD_START_ROUTINE func)
{
    X_ASSERT(handle_ == NULL, "Thread has already been created")(handle_, id_);

    handle_ = ::CreateThread(NULL, stackSize, func, this, CREATE_SUSPENDED, (LPDWORD)&id_);

    return handle_ != NULL;
}

uint32_t __stdcall Thread::ThreadFunction_(void* threadInstance)
{
    Thread* pThis = reinterpret_cast<Thread*>(threadInstance);
    X_ASSERT_NOT_NULL(pThis);

    uint32_t ret = pThis->function_(*pThis);
    pThis->state_ = State::FINISHED;
    return ret;
}

// ~static

// -------------------------------------------------------------------------------

ThreadAbstract::ThreadAbstract()
{
}

ThreadAbstract::~ThreadAbstract()
{
    thread_.destroy();
}

void ThreadAbstract::create(const char* pName, uint32_t stackSize)
{
    thread_.create(pName ? pName : "AbstractThread", stackSize);
    thread_.setData(this);
}

void ThreadAbstract::start(void)
{
    thread_.start(ThreadFunc);
}

void ThreadAbstract::stop(void)
{
    thread_.stop();
}

void ThreadAbstract::join(void)
{
    thread_.join();
}

uint32_t ThreadAbstract::getID(void) const
{
    return thread_.getID();
}

Thread::State::Enum ThreadAbstract::getState(void) const
{
    return thread_.getState();
}

void ThreadAbstract::cancelSynchronousIo(void)
{
    thread_.cancelSynchronousIo();
}

Thread::ReturnValue ThreadAbstract::ThreadFunc(const Thread& thread)
{
    ThreadAbstract* pThis = reinterpret_cast<ThreadAbstract*>(thread.getData());
    return pThis->ThreadRun(thread);
}

X_NAMESPACE_END