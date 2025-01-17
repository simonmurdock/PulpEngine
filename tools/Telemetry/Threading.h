#pragma once

struct adopt_lock_t
{
};

constexpr adopt_lock_t adopt_lock{};


template<class ThreadPolicy>
class ScopedLock
{
public:
    inline explicit ScopedLock(ThreadPolicy& policy) :
        policy_(policy)
    {
        policy_.Enter();
    }

    inline ScopedLock(ThreadPolicy& policy, adopt_lock_t) :
        policy_(policy)
    {
    }

    inline ~ScopedLock(void) {
        policy_.Leave();
    }

private:
    TELEM_NO_COPY(ScopedLock);
    TELEM_NO_ASSIGN(ScopedLock);

    ThreadPolicy& policy_;
};

TELEM_DISABLE_WARNING(4324) // structure was padded due to alignment specifier

TELEM_ALIGNED_SYMBOL(class CriticalSection, 64)
{
public:
    typedef ScopedLock<CriticalSection> ScopedLock;

public:
    inline CriticalSection(void);
    inline explicit CriticalSection(tt_int32 spinCount);

    inline ~CriticalSection(void);

    inline void Enter(void);
    inline bool TryEnter(void);
    inline void Leave(void);

    inline void SetSpinCount(tt_int32 count);

    inline CRITICAL_SECTION* GetNativeObject(void);

private:
    CRITICAL_SECTION cs_;
};

TELEM_ENABLE_WARNING(4324)


CriticalSection::CriticalSection(void)
{
    InitializeCriticalSection(&cs_);
}

CriticalSection::CriticalSection(tt_int32 spinCount)
{
    if (!InitializeCriticalSectionAndSpinCount(&cs_, spinCount)) {
        // TODO:
    }
}

CriticalSection::~CriticalSection(void)
{
    DeleteCriticalSection(&cs_);
}

inline void CriticalSection::Enter(void)
{
    EnterCriticalSection(&cs_);
}

inline bool CriticalSection::TryEnter(void)
{
    return TryEnterCriticalSection(&cs_) != 0;
}

inline void CriticalSection::Leave(void)
{
    LeaveCriticalSection(&cs_);
}

void CriticalSection::SetSpinCount(tt_int32 count)
{
    tt_int32 previous = SetCriticalSectionSpinCount(&cs_, count);
    TELEM_UNUSED(previous);
}

inline CRITICAL_SECTION* CriticalSection::GetNativeObject(void)
{
    return &cs_;
}
