#pragma once

#ifndef _X_CONSOLE_NULL_DEF_H_
#define _X_CONSOLE_NULL_DEF_H_

#include <IConsole.h>

X_NAMESPACE_BEGIN(core)

class XConsoleNULL : public IConsole
{
public:
    XConsoleNULL();

    virtual ~XConsoleNULL() X_FINAL;

    virtual void registerVars(void) X_FINAL;
    virtual void registerCmds(void) X_FINAL;

    // called at start when not much else exists, just so subsystems can register vars
    virtual bool init(ICore* pCore, bool basic) X_FINAL;
    // finialize any async init tasks.
    virtual bool asyncInitFinalize(void) X_FINAL;
    virtual bool loadRenderResources(void) X_FINAL;

    virtual void shutDown(void) X_FINAL;
    virtual void freeRenderResources(void) X_FINAL;
    virtual void saveChangedVars(void) X_FINAL; // saves vars with 'SAVE_IF_CHANGED' if modified.

    virtual void dispatchRepeateInputEvents(core::FrameTimeData& time) X_FINAL;
    virtual void runCmds(void) X_FINAL;
    virtual void draw(core::FrameTimeData& time) X_FINAL;

    virtual consoleState::Enum getVisState(void) const X_FINAL;

    virtual ICVar* registerString(core::string_view name, core::string_view value, VarFlags Flags, core::string_view desc) X_FINAL;
    virtual ICVar* registerInt(core::string_view name, int Value, int Min, int Max, VarFlags Flags, core::string_view desc) X_FINAL;
    virtual ICVar* registerFloat(core::string_view name, float Value, float Min, float Max, VarFlags Flags, core::string_view desc) X_FINAL;

    // refrenced based, these are useful if we want to use the value alot so we just register it's address.
    virtual ICVar* registerRef(core::string_view name, float* src, float defaultvalue, float Min, float Max, VarFlags nFlags, core::string_view desc) X_FINAL;
    virtual ICVar* registerRef(core::string_view name, int* src, int defaultvalue, int Min, int Max, VarFlags nFlags, core::string_view desc) X_FINAL;
    virtual ICVar* registerRef(core::string_view name, Color* src, Color defaultvalue, VarFlags nFlags, core::string_view desc) X_FINAL;
    virtual ICVar* registerRef(core::string_view name, Vec3f* src, Vec3f defaultvalue, VarFlags flags, core::string_view desc) X_FINAL;

    virtual ICVar* getCVar(core::string_view name) X_FINAL;

    virtual void unregisterVariable(core::string_view varName) X_FINAL;
    virtual void unregisterVariable(ICVar* pVar) X_FINAL;

    virtual void registerCommand(core::string_view name, ConsoleCmdFunc func, VarFlags Flags, core::string_view desc) X_FINAL;
    virtual void unRegisterCommand(core::string_view name) X_FINAL;

    virtual void exec(core::string_view command) X_FINAL;

    //	virtual void ConfigExec(const char* command) X_FINAL;
    virtual bool loadAndExecConfigFile(core::string_view fileName) X_FINAL;

    // Loggging
    virtual void addLineToLog(const char* pStr, uint32_t length) X_FINAL;
    virtual int getLineCount(void) const X_FINAL;
    // ~Loggging
};

X_NAMESPACE_END

#endif // !_X_CONSOLE_NULL_DEF_H_