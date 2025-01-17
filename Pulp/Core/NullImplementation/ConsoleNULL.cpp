#include "stdafx.h"
#include "ConsoleNULL.h"

X_NAMESPACE_BEGIN(core)

XConsoleNULL::XConsoleNULL()
{
}

XConsoleNULL::~XConsoleNULL()
{
}

void XConsoleNULL::registerVars(void)
{
}

void XConsoleNULL::registerCmds(void)
{
}

bool XConsoleNULL::init(ICore* pCore, bool basic)
{
    X_UNUSED(pCore);
    X_UNUSED(basic);
    return true;
}

bool XConsoleNULL::asyncInitFinalize(void)
{
    return true;
}

bool XConsoleNULL::loadRenderResources(void)
{
    return true;
}

void XConsoleNULL::shutDown(void)
{
}

void XConsoleNULL::freeRenderResources(void)
{
}

void XConsoleNULL::saveChangedVars(void)
{
}

void XConsoleNULL::dispatchRepeateInputEvents(core::FrameTimeData& time)
{
    X_UNUSED(time);
}

void XConsoleNULL::runCmds(void)
{
}

void XConsoleNULL::draw(core::FrameTimeData& time)
{
    X_UNUSED(time);
}

consoleState::Enum XConsoleNULL::getVisState(void) const
{
    return consoleState::CLOSED;
}

ICVar* XConsoleNULL::registerString(core::string_view name, core::string_view value, VarFlags Flags,
    core::string_view desc)
{
    X_UNUSED(name);
    X_UNUSED(value);
    X_UNUSED(Flags);
    X_UNUSED(desc);
    return nullptr;
}

ICVar* XConsoleNULL::registerInt(core::string_view name, int Value, int Min, int Max,
    VarFlags Flags, core::string_view desc)
{
    X_UNUSED(name);
    X_UNUSED(Value);
    X_UNUSED(Min);
    X_UNUSED(Max);
    X_UNUSED(Flags);
    X_UNUSED(desc);
    return nullptr;
}

ICVar* XConsoleNULL::registerFloat(core::string_view name, float Value, float Min, float Max,
    VarFlags flags, core::string_view desc)
{
    X_UNUSED(name);
    X_UNUSED(Value);
    X_UNUSED(Min);
    X_UNUSED(Max);
    X_UNUSED(flags);
    X_UNUSED(desc);
    return nullptr;
}

// refrenced based, these are useful if we want to use the value alot so we just register it's address.
ICVar* XConsoleNULL::registerRef(core::string_view name, float* src, float defaultvalue,
    float Min, float Max, VarFlags flags, core::string_view desc)
{
    X_UNUSED(name);
    X_UNUSED(src);
    X_UNUSED(defaultvalue);
    X_UNUSED(Min);
    X_UNUSED(Max);
    X_UNUSED(flags);
    X_UNUSED(desc);

    *src = defaultvalue;

    return nullptr;
}

ICVar* XConsoleNULL::registerRef(core::string_view name, int* src, int defaultvalue,
    int Min, int Max, VarFlags flags, core::string_view desc)
{
    X_UNUSED(name);
    X_UNUSED(src);
    X_UNUSED(defaultvalue);
    X_UNUSED(Min);
    X_UNUSED(Max);
    X_UNUSED(flags);
    X_UNUSED(desc);

    *src = defaultvalue;

    return nullptr;
}

ICVar* XConsoleNULL::registerRef(core::string_view name, Color* src, Color defaultvalue,
    VarFlags flags, core::string_view desc)
{
    X_UNUSED(name);
    X_UNUSED(src);
    X_UNUSED(defaultvalue);
    X_UNUSED(flags);
    X_UNUSED(desc);

    *src = defaultvalue;
    return nullptr;
}

ICVar* XConsoleNULL::registerRef(core::string_view name, Vec3f* src, Vec3f defaultvalue,
    VarFlags flags, core::string_view desc)
{
    X_UNUSED(name);
    X_UNUSED(src);
    X_UNUSED(defaultvalue);
    X_UNUSED(flags);
    X_UNUSED(desc);

    *src = defaultvalue;

    return nullptr;
}

ICVar* XConsoleNULL::getCVar(core::string_view name)
{
    X_UNUSED(name);

    return nullptr;
}

void XConsoleNULL::unregisterVariable(core::string_view varName)
{
    X_UNUSED(varName);
}

void XConsoleNULL::unregisterVariable(ICVar* pVar)
{
    X_UNUSED(pVar);
}

void XConsoleNULL::registerCommand(core::string_view name, ConsoleCmdFunc func, VarFlags Flags,
    core::string_view desc)
{
    X_UNUSED(name,func,Flags,desc);
}

void XConsoleNULL::unRegisterCommand(core::string_view name)
{
    X_UNUSED(name);
}

void XConsoleNULL::exec(core::string_view command)
{
    X_UNUSED(command);
}

bool XConsoleNULL::loadAndExecConfigFile(core::string_view fileName)
{
    X_UNUSED(fileName);
    return true;
}

/*
void XConsoleNULL::ConfigExec(const char* command)
{
X_UNUSED(command);

}*/

// Loggging
void XConsoleNULL::addLineToLog(const char* pStr, uint32_t length)
{
    X_UNUSED(pStr);
    X_UNUSED(length);
}

int XConsoleNULL::getLineCount(void) const
{
    return 0;
}
// ~Loggging

X_NAMESPACE_END