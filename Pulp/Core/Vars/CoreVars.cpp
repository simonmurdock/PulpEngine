#include "stdafx.h"
#include "CoreVars.h"

#include "Platform\Window.h"

#include <IConsole.h>
#include <Threading\JobSystem2.h>

X_NAMESPACE_BEGIN(core)

CoreVars::CoreVars() :
    pWinPosX_(nullptr),
    pWinPosY_(nullptr),
    pWinWidth_(nullptr),
    pWinHeight_(nullptr),
    pWinCustomFrame_(nullptr)
{
    // defaults.
    schedulerNumThreads_ = 0;
    coreFastShutdown_ = 0;
    coreEventDebug_ = 0;

    winXPos_ = 10;
    winYPos_ = 10;
    winWidth_ = 800;
    winHeight_ = 600;

#if TTELEMETRY_ENABLED
    telemPause_ = 0;
#endif // !TTELEMETRY_ENABLED

    fullscreen_ = 0;
    monitor_ = -1; // detect default monitor.
    videoMode_ = -1;
}

void CoreVars::registerVars(void)
{
    ADD_CVAR_REF("core_scheduler_threads", schedulerNumThreads_, schedulerNumThreads_, 0, core::V2::JobSystem::HW_THREAD_MAX, VarFlag::SYSTEM | VarFlag::SAVE_IF_CHANGED,
        "Number of threads to create for scheduler. 0=auto");

    ADD_CVAR_REF("core_fast_shutdown", coreFastShutdown_, coreFastShutdown_, 0, 1, VarFlag::SYSTEM | VarFlag::SAVE_IF_CHANGED,
        "Skips cleanup logic for fast shutdown, when off everything is correctly shutdown and released before exit. 0=off 1=on");
    ADD_CVAR_REF("core_event_debug", coreEventDebug_, coreEventDebug_, 0, 1, VarFlag::SYSTEM | VarFlag::SAVE_IF_CHANGED,
        "Debug messages for core events. 0=off 1=on");

    core::Window::Rect desktop = core::Window::GetDesktopRect();

    auto minWinPos = std::numeric_limits<int16_t>::min();

    pWinPosX_ = ADD_CVAR_REF("win_x_pos", winXPos_, winXPos_, minWinPos, desktop.getWidth(),
        VarFlag::SYSTEM | VarFlag::ARCHIVE, "Game window position x");
    pWinPosY_ = ADD_CVAR_REF("win_y_pos", winYPos_, winYPos_, minWinPos, desktop.getHeight(),
        VarFlag::SYSTEM | VarFlag::ARCHIVE, "Game window position y");
    pWinWidth_ = ADD_CVAR_REF("win_width", winWidth_, winWidth_, 800, 1,
        VarFlag::SYSTEM | VarFlag::ARCHIVE, "Game window width");
    pWinHeight_ = ADD_CVAR_REF("win_height", winHeight_, winHeight_, 600, 1,
        VarFlag::SYSTEM | VarFlag::ARCHIVE, "Game window height");

    pWinCustomFrame_ = ADD_CVAR_INT("win_custom_Frame", 1, 0, 1,
        VarFlag::SYSTEM | VarFlag::SAVE_IF_CHANGED, "Enable / disable the windows custom frame");

    ADD_CVAR_REF("win_fullscreen", fullscreen_, fullscreen_, -1, 2,
        VarFlag::SYSTEM | VarFlag::ARCHIVE, "0 = windowed 1 = fullscreen");
    ADD_CVAR_REF("win_monitor", monitor_, monitor_, -1, 64,
        VarFlag::SYSTEM | VarFlag::ARCHIVE, "-1 = detech default, >= 0 specific");
    ADD_CVAR_REF("win_mode", videoMode_, videoMode_, -1, -2,
        VarFlag::SYSTEM | VarFlag::ARCHIVE, "Game window video mode");

    const char* pVersionStr = (X_ENGINE_NAME "Engine " X_PLATFORM_STR "-" X_CPUSTRING " Version " X_ENGINE_VERSION_STR);
    ADD_CVAR_STRING("version", pVersionStr,
        VarFlag::SYSTEM | VarFlag::READONLY, "Engine Version");
    ADD_CVAR_STRING("build_ref", X_STRINGIZE(X_ENGINE_BUILD_REF),
        VarFlag::SYSTEM | VarFlag::READONLY, "Engine Version");
    ADD_CVAR_STRING("build_date", X_ENGINE_BUILD_DATE,
        VarFlag::SYSTEM | VarFlag::READONLY, "Engine Build Date");

#if TTELEMETRY_ENABLED
    ADD_CVAR_REF("telem_pause", telemPause_, telemPause_, 0, 1, VarFlag::SYSTEM, "Pause telemetry collection");
#endif // !TTELEMETRY_ENABLED
}

void CoreVars::setFullScreen(int32_t val)
{
    fullscreen_ = val;
}

void CoreVars::setMonitorIdx(int32_t val)
{
    monitor_ = val;
}

void CoreVars::updateWinPos(int32_t x, int32_t y)
{
    winXPos_ = x;
    winYPos_ = y;
}

void CoreVars::updateWinDim(int32_t width, int32_t height)
{
    winWidth_ = width;
    winHeight_ = height;
}

X_NAMESPACE_END
