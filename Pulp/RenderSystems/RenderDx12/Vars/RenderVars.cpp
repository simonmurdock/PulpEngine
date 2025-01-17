#include "stdafx.h"
#include "RenderVars.h"

#include <IConsole.h>

X_NAMESPACE_BEGIN(render)

RenderVars::RenderVars() :
    pNativeRes_(nullptr)
{
    varsRegistered_ = false;

    // defaults
    debugLayer_ = 0; // the debug override is not done here, otherwise it would not override config values.

    clearColor_ = Color(0.057f, 0.221f, 0.400f);
}

void RenderVars::registerVars(void)
{
    X_ASSERT(!varsRegistered_, "Vars already init")(varsRegistered_);

    ADD_CVAR_REF("r_d3d_debug_layer", debugLayer_, debugLayer_, 0, 1, core::VarFlag::RENDERER | core::VarFlag::SAVE_IF_CHANGED | core::VarFlag::RESTART_REQUIRED,
        "Enable d3d debug layer");

    ADD_CVAR_REF_COL("r_clear_col", clearColor_, clearColor_,
        core::VarFlag::RENDERER | core::VarFlag::SAVE_IF_CHANGED, "Back buffer clear color");

    pNativeRes_ = ADD_CVAR_STRING("r_native_res", "<unset>", core::VarFlag::RENDERER | core::VarFlag::READONLY,
        "The window render resolution");

    varsRegistered_ = true;
}

void RenderVars::setNativeRes(const Vec2i& res)
{
    core::StackString<64> buf;
    buf.appendFmt("%" PRIu32 "x%" PRIu32, res.x, res.y);

    pNativeRes_->ForceSet(core::string_view(buf));
}

X_NAMESPACE_END