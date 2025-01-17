#pragma once

#ifndef _X_CONSOLE_INTER_H_
#define _X_CONSOLE_INTER_H_

#include <Prepro\PreproStringize.h>
#include <Util\Delegate.h>

#include <Traits\FunctionTraits.h>

struct ICore;

X_NAMESPACE_BEGIN(core)

struct FrameTimeData;

class string_view;

static const size_t MAX_CONSOLE_VAR = 1024;
static const size_t MAX_CONSOLE_CMD = 256;
static const size_t MAX_CONSOLE_BINS = 128;

X_DECLARE_FLAGS(VarFlag)
(
    // ALL,			// all flags
    INT,    // variable is an integer
    FLOAT,  // variable is a float
    STRING, // variable is a string
    COLOR,
    VECTOR,
    BITFIELD,

    STATIC_DECL, // statically declared, not user created
    CHEAT,       // variable is considered a cheat
    READONLY,    // display only, cannot be set by user / config
    HIDDEN,      // not visible to the user.
    ARCHIVE,     // set to cause it to be saved to a config file
    MODIFIED,    // set when the variable is modified
    CONFIG,      // loaded or set from a config file.

    SAVE_IF_CHANGED, // saved to config if changed.
    RESTART_REQUIRED,

    // sub systems
    SYSTEM,
    SOUND,
    RENDERER,
    TOOL,       

    CPY_NAME,    // makes a copy of the name
    SINGLE_ARG   // prevents command arg tokenization
);

typedef Flags<VarFlag> VarFlags;

X_DECLARE_FLAG_OPERATORS(VarFlags);

X_DECLARE_ENUM(consoleState)
(
    CLOSED,
    OPEN,
    EXPANDED);

struct ICVar;

// console commands.
struct IConsoleCmdArgs
{
    virtual ~IConsoleCmdArgs() = default;
    // Gets number of arguments supplied to the command (including the command itself)
    virtual size_t GetArgCount(void) const X_ABSTRACT;
    // Gets argument by index, idx must be in 0 <= idx < GetArgCount()
    virtual core::string_view GetArg(size_t idx) const X_ABSTRACT;
    // returns a view from arg N to the end.
    virtual core::string_view GetArgToEnd(size_t idx) const X_ABSTRACT;
};

struct IKeyBindDumpSink
{
    virtual ~IKeyBindDumpSink() = default;
    virtual void OnKeyBindFound(core::string_view bind, core::string_view command) X_ABSTRACT;
};

// typedef core::traits::Function<void(ICVar*)> ConsoleVarFunc;
typedef core::traits::Function<void(IConsoleCmdArgs*)> ConsoleCmdOldFunc;

typedef core::Delegate<void(ICVar*)> ConsoleVarFunc;
typedef core::Delegate<void(IConsoleCmdArgs*)> ConsoleCmdFunc;

// The console interface
struct IConsole
{
    virtual ~IConsole() = default;

    virtual void registerVars(void) X_ABSTRACT;
    virtual void registerCmds(void) X_ABSTRACT;

    // called at start when not much else exists, just so subsystems can register vars
    virtual bool init(ICore* pCore, bool basic) X_ABSTRACT;
    // finalize any async init tasks.
    virtual bool asyncInitFinalize(void) X_ABSTRACT;
    virtual bool loadRenderResources(void) X_ABSTRACT;

    virtual void shutDown(void) X_ABSTRACT;
    virtual void freeRenderResources(void) X_ABSTRACT;
    virtual void saveChangedVars(void) X_ABSTRACT; // saves vars with 'SAVE_IF_CHANGED' if modified.

    // console set's it's own input repeat rate, that's timed instead of every frame.
    virtual void dispatchRepeateInputEvents(core::FrameTimeData& time) X_ABSTRACT;
    virtual void runCmds(void) X_ABSTRACT;
    virtual void draw(core::FrameTimeData& time) X_ABSTRACT;

    virtual consoleState::Enum getVisState(void) const X_ABSTRACT;

    // Register variables.
    virtual ICVar* registerString(core::string_view name, core::string_view value, VarFlags flags, core::string_view desc) X_ABSTRACT;
    virtual ICVar* registerInt(core::string_view name, int Value, int Min, int Max, VarFlags flags, core::string_view desc) X_ABSTRACT;
    virtual ICVar* registerFloat(core::string_view name, float Value, float Min, float Max, VarFlags flags, core::string_view desc) X_ABSTRACT;

    // referenced based, these are useful if we want to use the value a lot so we just register it's address.
    virtual ICVar* registerRef(core::string_view name, float* src, float defaultvalue, float Min, float Max, VarFlags flags, core::string_view desc) X_ABSTRACT;
    virtual ICVar* registerRef(core::string_view name, int* src, int defaultvalue, int Min, int Max, VarFlags flags, core::string_view desc) X_ABSTRACT;
    virtual ICVar* registerRef(core::string_view name, Color* src, Color defaultvalue, VarFlags flags, core::string_view desc) X_ABSTRACT;
    virtual ICVar* registerRef(core::string_view name, Vec3f* src, Vec3f defaultvalue, VarFlags flags, core::string_view desc) X_ABSTRACT;

    virtual ICVar* getCVar(core::string_view name) X_ABSTRACT;

    virtual void unregisterVariable(core::string_view varName) X_ABSTRACT;
    virtual void unregisterVariable(ICVar* pVar) X_ABSTRACT;

    virtual void registerCommand(core::string_view name, ConsoleCmdFunc func, VarFlags Flags, core::string_view desc) X_ABSTRACT;
    virtual void unRegisterCommand(core::string_view name) X_ABSTRACT;

    virtual void exec(core::string_view command) X_ABSTRACT;

    //	virtual void ConfigExec(const char* command) X_ABSTRACT;
    virtual bool loadAndExecConfigFile(core::string_view fileName) X_ABSTRACT;

    // Logging
    virtual void addLineToLog(const char* pStr, uint32_t length) X_ABSTRACT;
    virtual int getLineCount(void) const X_ABSTRACT;
    // ~Logging
};

struct ICVar
{
    typedef VarFlags FlagType;
    typedef char StrBuf[128];

    virtual ~ICVar() = default;

    virtual core::string_view GetName(void) const X_ABSTRACT;
    virtual core::string_view GetDesc(void) const X_ABSTRACT;
    virtual const char* GetDefaultStr(StrBuf& buf) const X_ABSTRACT;
    virtual void SetDesc(core::string_view desc) X_ABSTRACT;

    virtual int GetInteger(void) const X_ABSTRACT;
    virtual float GetFloat(void) const X_ABSTRACT;
    virtual const char* GetString(StrBuf& buf) const X_ABSTRACT;

    virtual void SetDefault(core::string_view str) X_ABSTRACT;
    virtual void Set(core::string_view str) X_ABSTRACT;
    virtual void ForceSet(core::string_view str) X_ABSTRACT;

    virtual void Set(const float f) X_ABSTRACT;
    virtual void Set(const int i) X_ABSTRACT;

    virtual FlagType GetFlags(void) const X_ABSTRACT;
    virtual FlagType SetFlags(FlagType flags) X_ABSTRACT;
    virtual void SetModified(void) X_ABSTRACT;
    virtual float GetMin(void) const X_ABSTRACT;
    virtual float GetMax(void) const X_ABSTRACT;
    virtual int32_t GetMinInt(void) const X_ABSTRACT;
    virtual int32_t GetMaxInt(void) const X_ABSTRACT;
    virtual int32_t GetDefaultInt(void) const X_ABSTRACT;

    virtual VarFlag::Enum GetType(void) const X_ABSTRACT;

    virtual void Reset(void) X_ABSTRACT; // reset to default value.

    virtual ICVar* SetOnChangeCallback(ConsoleVarFunc pChangeFunc) X_ABSTRACT;
    virtual ConsoleVarFunc GetOnChangeCallback(void) const X_ABSTRACT;
};

X_NAMESPACE_END

#endif // !_X_CONSOLE_INTER_H_
