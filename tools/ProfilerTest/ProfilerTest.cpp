#include "stdafx.h"
#include "ProfilerTest.h"
#include "EngineApp.h"

#include <Memory\MemoryTrackingPolicies\NoMemoryTracking.h>
#include <Memory\MemoryTrackingPolicies\ExtendedMemoryTracking.h>
#include <Memory\ThreadPolicies\MultiThreadPolicy.h>
#include <Memory\VirtualMem.h>
#include <Util/UniquePointer.h>

#include <Platform\Console.h>

#include <Time/StopWatch.h>
#include <Threading/SharedLock.h>

#include <../Telemetry/Telemetry.h>

#if TTELEMETRY_LINK
X_LINK_ENGINE_LIB("TelemetryLib")
#endif // !TTELEMETRY_LINK


#define _LAUNCHER
#include <ModuleExports.h>

#ifdef X_LIB
struct XRegFactoryNode* g_pHeadToRegFactories = nullptr;

X_LINK_ENGINE_LIB("Core")
X_LINK_ENGINE_LIB("RenderNull")

X_FORCE_SYMBOL_LINK("?s_factory@XEngineModule_Render@render@Potato@@0V?$XSingletonFactory@VXEngineModule_Render@render@Potato@@@@A");

#endif // !X_LIB


typedef core::MemoryArena<
    core::MallocFreeAllocator,
    core::MultiThreadPolicy<core::CriticalSection>,
#if X_ENABLE_MEMORY_DEBUG_POLICIES
    core::SimpleBoundsChecking,
    core::SimpleMemoryTracking,
    core::SimpleMemoryTagging
#else
    core::NoBoundsChecking,
    core::NoMemoryTracking,
    core::NoMemoryTagging
#endif // !X_ENABLE_MEMORY_SIMPLE_TRACKING
    >
    BenchmarkArena;

core::MemoryArenaBase* g_arena = nullptr;

namespace
{
#if TTELEMETRY_ENABLED
    telem::ContexHandle ctx;
#endif // !TTELEMETRY_ENABLED

    template<class ThreadPolicy>
    class ScopedLockTelemetry
    {
    public:
        inline explicit ScopedLockTelemetry(ThreadPolicy& policy) :
            policy_(policy)
        {
            policy_.Enter();
        }

        inline ~ScopedLockTelemetry(void) 
        {
            policy_.Leave();
        }

    private:
        X_NO_COPY(ScopedLockTelemetry);
        X_NO_ASSIGN(ScopedLockTelemetry);

        ThreadPolicy& policy_;
    };

    core::CriticalSection cs0, cs1;
    core::SharedLock sharedLock;
    volatile bool running = true;

    core::Thread::ReturnValue threadFunc(const core::Thread& thread)
    {
        X_UNUSED(thread);

        core::StopWatch timer;
        int32_t total = 0;

        ttSetLockName(ctx, &cs0, "lock cs0");
        ttSetLockName(ctx, &cs1, "lock cs1");

        TtCallStack stack;

        int32_t i = 0;
        while(running)
        {
            ttZone(ctx, "Sample zone with arg! %" PRIi32, i++);

            ttGetCallStack(ctx, stack);
            auto stackID = ttSendCallStack(ctx, &stack);

            ttMessage(ctx, TtMsgFlagsSeverityMsg, "Message with callstack: %t", stackID);

         //   core::ScopedLockShared locks0(sharedLock);

            {
                ttZoneFilterd(ctx, 200, "Sometimes filtered");

                // alloc me baby.
                ttAlloc(ctx, (void*)0x12345678, 0x300 * (i + 1), "Alloc some data!");
                ttFree(ctx, (void*)0x12345678);
            }

            ttPlot(ctx, TtPlotTypeTime, timer.GetMilliSeconds(), "A Plot!");

            for (int x = 0; x < 10; x++)
            {
                {
                    ttEnter(ctx, "empty zone %s!", "scope");
                    ttLeave(ctx);
                }

                {
                    ScopedLockTelemetry lock(cs0);

                    ttZone(ctx, "Sample Zone1");
                    total++;
                    core::Thread::sleep(0);
                }

                ttZone(ctx, "Sample Zone2");

                for (int j = 0; j < 50; j++)
                {
                    ttZone(ctx, "Outer loop Zone");

                    for (int y = 0; y < 2; y++)
                    {
                        ttEnter(ctx, "Inner loop zone");
                        ttLeave(ctx);
                    }
                }
            }

            ScopedLockTelemetry lock0(cs1);
            ScopedLockTelemetry lock1(cs1); // recursive lock check.
            ttZone(ctx, "Sleeping under lock");
            core::Thread::sleep(1);
        }

        ttFlush(ctx);

        auto ellapsed = timer.GetMilliSeconds();
        X_LOG0("ProfilerTest", "total %i %fms", total, ellapsed);

        return core::Thread::ReturnValue(0);
    }


    void LogFunc(void* pUserData, TtMsgFlags flags, const char* pMsgNullTerm, tt_int32 lenWithoutTerm)
    {
        X_UNUSED(pUserData);
        X_UNUSED(flags);
        X_UNUSED(pMsgNullTerm);
        X_UNUSED(lenWithoutTerm);

#if 1
        if (!gEnv->pLog) {
            return;
        }

        switch (flags & TtMsgFlagsSeverityMask)
        {
            case TtMsgFlagsSeverityMsg:
                X_LOG0("Telemetry", pMsgNullTerm);
                break;
            case TtMsgFlagsSeverityWarning:
                X_WARNING("Telemetry", pMsgNullTerm);
                break;
            case TtMsgFlagsSeverityError:
                X_ERROR("Telemetry", pMsgNullTerm);
                break;
       }
#endif
    }

} // namespace


int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR lpCmdLine,
    _In_ int nCmdShow)
{
    X_UNUSED(hPrevInstance, nCmdShow);

    {
        EngineApp engine;

        if (engine.Init(hInstance, lpCmdLine)) {
            X_ASSERT_NOT_NULL(gEnv);
            X_ASSERT_NOT_NULL(gEnv->pCore);

            BenchmarkArena::AllocationPolicy allocator;
            BenchmarkArena arena(&allocator, "ProfileTestArena");

            g_arena = &arena;

            ctx = gEnv->ctx;

            // now engine logging is init redirect logs here.
            ttSetContextLogFunc(ctx, LogFunc, nullptr);

            gEnv->pConsoleWnd->redirectSTD();

            const int32_t numThreads = 4;

            core::Thread thread[numThreads];

            ttLog(ctx, "Hello stu!");
            ttWarning(ctx, "Can't find stu");
            ttError(ctx, "Goat has no boat %" PRIi32 " %" PRIi64 " %" PRIi64 " %" PRIi64 " %" PRIi64 " %f %f %s",
                124_i32, 5226262_i64, 16_i64, 32_i64, 64_i64, 0.353f, 515125.0203503557575, "meow meow");

            ttPlot(ctx, TtPlotTypeInteger, 1, "meow?");
            ttPlotF32(ctx, TtPlotTypeInteger, 1, "cow?");
            ttPlotF64(ctx, TtPlotTypeInteger, 1, "pickle?");
            ttPlotI32(ctx, TtPlotTypeInteger, 1, "nickle?");
            ttPlotI64(ctx, TtPlotTypeInteger, 1, "pizza?");
            ttPlotU32(ctx, TtPlotTypeInteger, 1, "noodles?");
            ttPlotU64(ctx, TtPlotTypeInteger, 1, "bananan?");

            core::StackString256 name;
            for (int32_t i = 0; i < numThreads; i++) {
                name.setFmt("Worker %" PRIi32, i);

                thread[i].create(name.c_str());
                thread[i].start(threadFunc);

                // TODO: Support dynamic strings.
                ttSetThreadName(ctx, thread[i].getID(), "%s", name.c_str());
                ttSetThreadGroup(gEnv->ctx, thread[i].getID(), 1);
            }

            float numMS = 1000 * 1;

            core::StopWatch timer;

            auto time0 = ttFastTime();
            core::Thread::sleep(50);
            auto time1 = ttFastTime();
            auto ellapsed = ttFastTimeToMs(ctx, time1 - time0);

            X_LOG0("Main", "fasttime check: %f", ellapsed);

            // main loop
            while(timer.GetMilliSeconds() < numMS)
            {
                ttTick(ctx);
                ttZone(ctx, "Frame");

                {
                    ttZone(ctx, "Sleep");
                    core::Thread::sleep(16);
                }

                X_LOG0("Main", "tick");
            }

            running = false;

            for (int32_t i = 0; i < numThreads; i++) {
                thread[i].join();
                thread[i].destroy();
            }

            // how many stalls?
            auto numStalls = ttGetStati(ctx, TtStatNumStalls);

            X_LOG0("Main", "TelemStalled: %" PRIi32, numStalls);

            gEnv->pConsoleWnd->pressToContinue();
        }
    }

    return 0;
}
