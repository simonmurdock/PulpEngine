#include "stdafx.h"

#include "Console.h"
#include "ConsoleVariable.h"

#include "String\StringTokenizer.h"
#include "Platform\ClipBoard.h"
#include "Containers\FixedArray.h"

#include "Memory\VirtualMem.h"

#include <ITimer.h>
#include <ICore.h>
#include <IFont.h>
#include <IRender.h>
#include <ILog.h>
#include <ICore.h>
#include <IFileSys.h>
#include <IFrameData.h>
#include <IPrimitiveContext.h>
#include <IFont.h>
#include <I3DEngine.h>
#include <IConfig.h>

#include "Platform\Window.h"

#include <algorithm>

X_NAMESPACE_BEGIN(core)

namespace
{
    static const size_t VAR_ALLOCATION_SIZE = core::Max(
        core::Max(
            core::Max(
                core::Max(
                    core::Max(
                        core::Max(
                            core::Max(
                                core::Max(
                                    sizeof(CVarString<CVarBaseConst>),
                                    sizeof(CVarInt<CVarBaseConst>)),
                                sizeof(CVarFloat<CVarBaseConst>)),
                            sizeof(CVarString<CVarBaseHeap>)),
                        sizeof(CVarInt<CVarBaseHeap>)),
                    sizeof(CVarFloat<CVarBaseHeap>)),
                sizeof(CVarFloatRef)),
            sizeof(CVarIntRef)),
        sizeof(CVarColRef));

    static const size_t VAR_ALLOCATION_ALIGNMENT = core::Max(
        core::Max(
            core::Max(
                core::Max(
                    core::Max(
                        core::Max(
                            core::Max(
                                core::Max(
                                    X_ALIGN_OF(CVarString<CVarBaseConst>),
                                    X_ALIGN_OF(CVarInt<CVarBaseConst>)),
                                X_ALIGN_OF(CVarFloat<CVarBaseConst>)),
                            X_ALIGN_OF(CVarString<CVarBaseHeap>)),
                        X_ALIGN_OF(CVarInt<CVarBaseHeap>)),
                    X_ALIGN_OF(CVarFloat<CVarBaseHeap>)),
                X_ALIGN_OF(CVarFloatRef)),
            X_ALIGN_OF(CVarIntRef)),
        X_ALIGN_OF(CVarColRef));

    static void sortVarsByName(core::Array<const core::ICVar*>& vars)
    {
        std::sort(vars.begin(), vars.end(),
            [](const core::ICVar* a, const core::ICVar* b) {
                return a->GetName() < b->GetName();
            }
        );
    }

    static void sortCmdsByName(core::Array<const core::ConsoleCommand*>& vars)
    {
        std::sort(vars.begin(), vars.end(),
            [](const core::ConsoleCommand* a, const core::ConsoleCommand* b) {
                return a->name.compareInt(b->name) < 0;
            }
        );
    }

    class CommandParser
    {
    public:
        CommandParser(const char* pBegin, const char* pEnd) :
            begin_(pBegin),
            end_(pEnd)
        {
        }

        bool extractCommand(core::StringRange<char>& cmd)
        {
            const char* cur = begin_;
            const char* end = end_;

            while (cur < end) {
                char ch = *cur++;
                switch (ch) {
                    case '\'':
                    case '\"':
                        while ((*cur++ != ch) && cur < end)
                            ;
                        break;
                    case '\n':
                    case '\r':
                    case ';':
                    case '\0': {
                        // we class this as end of a command.
                        cmd = StringRange<char>(begin_, cur);
                        begin_ = cur;
                        return true;
                    } break;
                }
            }

            // got anything?
            if (begin_ < cur) {
                cmd = StringRange<char>(begin_, cur);
                begin_ = cur;
                return true;
            }
            return false;
        }

    private:
        const char* begin_;
        const char* end_;
    };

    struct AutoResult
    {
        AutoResult() :
            AutoResult(nullptr, nullptr, nullptr)
        {
        }
        AutoResult(const char* pName, ICVar* var, ConsoleCommand* pCmd) :
            pName(pName),
            var(var),
            pCmd(pCmd)
        {
        }

        X_INLINE bool operator<(const AutoResult& oth)
        {
            return strcmp(pName, oth.pName) < 0;
        }

    public:
        const char* pName;
        ICVar* var;
        ConsoleCommand* pCmd;
    };

    const int32_t CONSOLE_INPUT_FONT_SIZE = 18;
    const int32_t CONSOLE_DEFAULT_LOG_FONT_SIZE = 14;

    const float CONSOLE_INPUT_LINE_HIEGHT = 1.1f;
    const float CONSOLE_DEFAULT_LOG_LINE_HIEGHT = 1.1f;


    bool cvarModifyBegin(ICVar* pCVar, ExecSource::Enum source)
    {
        X_ASSERT_NOT_NULL(pCVar);

        ICVar::FlagType flags = pCVar->GetFlags();

        if (flags.IsSet(VarFlag::READONLY)) {
            auto name = pCVar->GetName();
            X_ERROR("Console", "can't set value of read only cvar: %.*s", name.length(), name.data());
            return false;
        }

        if (source == ExecSource::CONFIG) {
            flags.Set(VarFlag::CONFIG);
            pCVar->SetFlags(flags);
        }

        return true;
    }
} // namespace

// ==================================================

ConsoleCommand::ConsoleCommand() // flags default con is (0)
{
}

// ==================================================

ConsoleCommandArgs::ConsoleCommandArgs(const CommandStr& line, ParseFlags flags) :
    argNum_(0),
    str_(line)
{
    TokenizeString(flags);
}

ConsoleCommandArgs::~ConsoleCommandArgs()
{
}

size_t ConsoleCommandArgs::GetArgCount(void) const
{
    return argNum_;
}

core::string_view ConsoleCommandArgs::GetArg(size_t idx) const
{
    X_ASSERT(idx < argNum_, "Argument index out of range")(argNum_, idx);
    auto arg = argv_[idx];

    X_ASSERT(arg.first + arg.second <= static_cast<int32_t>(str_.length()), "Argument out of range")(arg.first + arg.second, str_.length());
    return core::string_view(str_.data() + arg.first, arg.second);
}

core::string_view ConsoleCommandArgs::GetArgToEnd(size_t idx) const
{
    X_ASSERT(idx < argNum_, "Argument index out of range")(argNum_, idx);
    auto arg = argv_[idx];

    X_ASSERT(arg.first < static_cast<int32_t>(str_.length()), "Argument out of range")(arg.first, str_.length());
    return core::string_view(str_.data() + arg.first, str_.end());
}

void ConsoleCommandArgs::TokenizeString(ParseFlags flags)
{
    if (str_.isEmpty()) {
        return;
    }

    auto* pBegin = str_.begin();
    auto* pCur = pBegin;
    auto* pEnd = str_.end();

    if (flags.IsSet(ParseFlag::SINGLE_ARG)) 
    {
        // just need to split out the command.
        while (*pCur != ' ' && pCur < pEnd) {
            ++pCur;
        }

        const auto commandNameLen = pCur - pBegin;
        
        argv_[argNum_] = {0_i32, static_cast<int32_t>(commandNameLen)};
        argNum_++;

        const auto trailing = str_.length() - commandNameLen;
        if (trailing == 0) {
            return;
        }

        argv_[argNum_] = { static_cast<int32_t>(commandNameLen), static_cast<int32_t>(trailing) };
        argNum_++;
        return;
    }

    // want to split based on spaces but we join quoted strings.
    auto* pStart = pCur;

    while (pCur < pEnd)
    {
        if (argNum_ == MAX_COMMAND_ARGS) {
            return;
        }

        char ch = *pCur++;

        switch (ch) {
            case ' ': {
                pStart = pCur;
                break;
            }
            case '\'':
            case '\"': {

                // is there a matching close?
                auto* pClose = strUtil::Find(pCur, pEnd, ch);
                if (pClose)
                {
                    auto startIdx = std::distance(pBegin, pStart);
                    auto length = std::distance(pStart, pClose);

                    argv_[argNum_] = { static_cast<int32_t>(startIdx), static_cast<int32_t>(length) };
                    argNum_++;

                    pCur = pClose + 1;
                    pStart = pCur;
                    break;
                }
                
                // ignore it.
                [[fallthrough]];
            }

            default: {

                // next char is a space?
                if (*pCur == ' ' || pCur == pEnd)
                {
                    auto startIdx = std::distance(pBegin, pStart);
                    auto length = std::distance(pStart, pCur);

                    if (*pStart == '#') {
                        // TODO: support var replacement again.
                        // so: `echo fps limit is: #maxfps` works

                    }

                    argv_[argNum_] = { static_cast<int32_t>(startIdx), static_cast<int32_t>(length) };
                    argNum_++;

                    pCur = pCur + 1;
                    pStart = pCur;
                }

                break;
            }
        }
    }
}

//////////////////////////////////////////////////////////////////////////

const char* XConsole::CMD_HISTORY_FILE_NAME = "cmdHistory.txt";
const char* XConsole::USER_CFG_FILE_NAME = "user_config.cfg";

//////////////////////////////////////////////////////////////////////////

XConsole::ExecCommand::ExecCommand(const string& cmd, ExecSource::Enum src, bool sm) :
    command(cmd),
    source(src),
    silentMode(sm)
{
}

XConsole::Cursor::Cursor() :
    curTime(0.f),
    displayTime(0.5f),
    draw(false)
{
}

//////////////////////////////////////////////////////////////////////////

XConsole::XConsole(core::MemoryArenaBase* arena) :
    arena_(arena),
    varHeap_(
        bitUtil::RoundUpToMultiple<size_t>(
            VarPool::getMemoryRequirement(VAR_ALLOCATION_SIZE) * MAX_CONSOLE_VAR,
            VirtualMem::GetPageSize())),
    varAllocator_(varHeap_.start(), varHeap_.end(),
        VarPool::getMemoryRequirement(VAR_ALLOCATION_SIZE),
        VarPool::getMemoryAlignmentRequirement(VAR_ALLOCATION_ALIGNMENT),
        VarPool::getMemoryOffsetRequirement()),
    varArena_(&varAllocator_, "VarArena"),
    cmdHistory_(arena_),
    consoleLog_(arena_),
    varMap_(arena_, bitUtil::NextPowerOfTwo(MAX_CONSOLE_VAR * 2)),
    cmdMap_(arena_, bitUtil::NextPowerOfTwo(MAX_CONSOLE_CMD * 2)),
    binds_(arena_, bitUtil::NextPowerOfTwo(MAX_CONSOLE_BINS * 2)),
    configCmds_(arena_, 256),
    varArchive_(arena_, 256),
    pendingCmdsQueue_(arena_),
    coreEventListernRegd_(false),
    historyLoadPending_(false),
    historyFileSize_(0),
    historyFileBuf_(arena_)
{
    historyPos_ = 0;
    cursorPos_ = 0;
    scrollPos_ = 0;

    arena_->addChildArena(&varArena_);

    consoleState_ = consoleState::CLOSED;

    pCore_ = nullptr;
    pFont_ = nullptr;
    pRender_ = nullptr;
    pPrimContext_ = nullptr;

    // Auto goat a boat.
    autoCompleteNum_ = 0;
    autoCompleteIdx_ = -1;
    autoCompleteSelect_ = false;

    repeatEventTimer_ = TimeVal(0ll);
    repeatEventInterval_ = TimeVal(0.025f);
    repeatEventInitialDelay_ = TimeVal(0.5f);

    ttSetLockName(gEnv->ctx, &historyFileLock_, "HistoryFileLock");
}

XConsole::~XConsole()
{
    arena_->removeChildArena(&varArena_);
}

void XConsole::registerVars(void)
{
    ADD_CVAR_REF_NO_NAME(console_debug, 0, 0, 1, VarFlag::SYSTEM | VarFlag::CHEAT,
        "Debugging for console operations. 0=off 1=on");
    ADD_CVAR_REF_NO_NAME(console_case_sensitive, 0, 0, 1, VarFlag::SYSTEM | VarFlag::SAVE_IF_CHANGED,
        "Console input auto complete is case-sensitive");
    ADD_CVAR_REF_NO_NAME(console_save_history, 1, 0, 1, VarFlag::SYSTEM | VarFlag::SAVE_IF_CHANGED,
        "Saves command history to file");
    ADD_CVAR_REF_NO_NAME(console_buffer_size, 1000, 1, 10000, VarFlag::SYSTEM | VarFlag::SAVE_IF_CHANGED,
        "Size of the log buffer");
    ADD_CVAR_REF_NO_NAME(console_output_draw_channel, 1, 0, 1, VarFlag::SYSTEM | VarFlag::SAVE_IF_CHANGED,
        "Draw the channel in a different color. 0=disabled 1=enabled");
    ADD_CVAR_REF_NO_NAME(console_cursor_skip_color_codes, 1, 0, 1, VarFlag::SYSTEM | VarFlag::SAVE_IF_CHANGED,
        "Skips over the color codes when moving cursor.");
    ADD_CVAR_REF_NO_NAME(console_disable_mouse, 2, 0, 2, VarFlag::SYSTEM | VarFlag::SAVE_IF_CHANGED,
        "Disable mouse input when console open. 1=expanded only 2=always");

    ADD_CVAR_REF_NO_NAME(console_output_font_size, CONSOLE_DEFAULT_LOG_FONT_SIZE, 1, 256, VarFlag::SYSTEM | VarFlag::SAVE_IF_CHANGED,
        "Font size of log messages");
    ADD_CVAR_REF_NO_NAME(console_output_font_line_height, CONSOLE_DEFAULT_LOG_LINE_HIEGHT, 0.1f, 10.f, VarFlag::SYSTEM | VarFlag::SAVE_IF_CHANGED,
        "Line height of log messages");

    ADD_CVAR_REF_COL_NO_NAME(console_cmd_color, Color(0.0f, 0.5f, 0.5f, 1.f),
        VarFlag::SYSTEM | VarFlag::SAVE_IF_CHANGED, "Console cmd color");
    ADD_CVAR_REF_COL_NO_NAME(console_input_box_color, Color(0.3f, 0.3f, 0.3f, 0.75f),
        VarFlag::SYSTEM | VarFlag::SAVE_IF_CHANGED, "Console input box color");
    ADD_CVAR_REF_COL_NO_NAME(console_input_box_color_border, Color(0.1f, 0.1f, 0.1f, 1.0f),
        VarFlag::SYSTEM | VarFlag::SAVE_IF_CHANGED, "Console input box color");
    ADD_CVAR_REF_COL_NO_NAME(console_output_box_color, Color(0.2f, 0.2f, 0.2f, 0.9f),
        VarFlag::SYSTEM | VarFlag::SAVE_IF_CHANGED, "Console output box color");
    ADD_CVAR_REF_COL_NO_NAME(console_output_box_color_border, Color(0.1f, 0.1f, 0.1f, 1.0f),
        VarFlag::SYSTEM | VarFlag::SAVE_IF_CHANGED, "Console output box color");
    ADD_CVAR_REF_COL_NO_NAME(console_output_box_channel_color, Color(0.15f, 0.15f, 0.15f, 0.5f),
        VarFlag::SYSTEM | VarFlag::SAVE_IF_CHANGED, "Console output box channel color");
    ADD_CVAR_REF_COL_NO_NAME(console_output_scroll_bar_color, Color(0.5f, 0.5f, 0.5f, 1.0f),
        VarFlag::SYSTEM | VarFlag::SAVE_IF_CHANGED, "Console output scroll bar color");
    ADD_CVAR_REF_COL_NO_NAME(console_output_scroll_bar_slider_color, Color(0.0f, 0.0f, 0.0f, 0.9f),
        VarFlag::SYSTEM | VarFlag::SAVE_IF_CHANGED, "Console output scroll bar slider color");
}

void XConsole::registerCmds(void)
{
    ADD_COMMAND_MEMBER("exec", this, XConsole, &XConsole::Command_Exec, VarFlag::SYSTEM, "executes a file(.cfg)");
    ADD_COMMAND_MEMBER("history", this, XConsole, &XConsole::Command_History, VarFlag::SYSTEM, "displays command history");
    ADD_COMMAND_MEMBER("help", this, XConsole, &XConsole::Command_Help, VarFlag::SYSTEM, "displays help info");
    ADD_COMMAND_MEMBER("listCmds", this, XConsole, &XConsole::Command_ListCmd, VarFlag::SYSTEM, "lists avaliable commands");
    ADD_COMMAND_MEMBER("listDvars", this, XConsole, &XConsole::Command_ListDvars, VarFlag::SYSTEM, "lists dvars");
    ADD_COMMAND_MEMBER("listDvarValues", this, XConsole, &XConsole::Command_ListDvarsValues, VarFlag::SYSTEM, "Same as 'listDvars' but showing values");
    ADD_COMMAND_MEMBER("exit", this, XConsole, &XConsole::Command_Exit, VarFlag::SYSTEM, "closes the game");
    ADD_COMMAND_MEMBER("quit", this, XConsole, &XConsole::Command_Exit, VarFlag::SYSTEM, "closes the game");
    ADD_COMMAND_MEMBER("echo", this, XConsole, &XConsole::Command_Echo, VarFlag::SYSTEM | VarFlag::SINGLE_ARG, "prints text in argument, prefix dvar's with # to print value");
    ADD_COMMAND_MEMBER("vreset", this, XConsole, &XConsole::Command_VarReset, VarFlag::SYSTEM, "resets a variable to it's default value");
    ADD_COMMAND_MEMBER("vdesc", this, XConsole, &XConsole::Command_VarDescribe, VarFlag::SYSTEM, "describes a variable");
    ADD_COMMAND_MEMBER("seta", this, XConsole, &XConsole::Command_SetVarArchive, VarFlag::SYSTEM, "set a var and flagging it to be archived");

    ADD_COMMAND_MEMBER("bind", this, XConsole, &XConsole::Command_Bind, VarFlag::SYSTEM, "binds a key to a action Eg: bind shift a 'echo hello';");
    ADD_COMMAND_MEMBER("clearBinds", this, XConsole, &XConsole::Command_BindsClear, VarFlag::SYSTEM, "clears all binds");
    ADD_COMMAND_MEMBER("listBinds", this, XConsole, &XConsole::Command_BindsList, VarFlag::SYSTEM, "lists all the binds");

    ADD_COMMAND_MEMBER("saveModifiedVars", this, XConsole, &XConsole::Command_SaveModifiedVars, VarFlag::SYSTEM, "Saves modifed vars");

    ADD_COMMAND_MEMBER("consoleShow", this, XConsole, &XConsole::Command_ConsoleShow, VarFlag::SYSTEM, "opens the console");
    ADD_COMMAND_MEMBER("consoleHide", this, XConsole, &XConsole::Command_ConsoleHide, VarFlag::SYSTEM, "hides the console");
    ADD_COMMAND_MEMBER("consoleToggle", this, XConsole, &XConsole::Command_ConsoleToggle, VarFlag::SYSTEM, "toggle the console");
}

// ------------------------------------

bool XConsole::init(ICore* pCore, bool basic)
{
    X_LOG0("Console", "Starting console");
    pCore_ = pCore;

    // dispatch command history loading.
    if (!basic) {
        pCore_->GetILog()->AddLogger(&logger_);

        coreEventListernRegd_ = pCore_->GetCoreEventDispatcher()->RegisterListener(this);

        if (console_save_history) {
            loadCmdHistory(true);
        }
    }
    else {
        // when in basic mode, don't save out.
        console_save_history = 0;
    }

    return true;
}

bool XConsole::asyncInitFinalize(void)
{
    // the history file should of loaded now, so lets parse it.
    {
        core::CriticalSection::ScopedLock lock(historyFileLock_);

        while (historyLoadPending_) {
            historyFileCond_.Wait(historyFileLock_);
        }

        if (historyFileSize_) {
            const char* pBegin = historyFileBuf_.get();
            const char* pEnd = pBegin + historyFileSize_;

            parseCmdHistory(pBegin, pEnd);

            historyFileBuf_.reset();
            historyFileSize_ = 0;
        }
    }

    return true;
}

bool XConsole::loadRenderResources(void)
{
    X_ASSERT_NOT_NULL(pCore_);
    X_ASSERT_NOT_NULL(pCore_->GetIFontSys());
    X_ASSERT_NOT_NULL(pCore_->GetIRender());
    X_ASSERT_NOT_NULL(pCore_->Get3DEngine());

    pFont_ = pCore_->GetIFontSys()->getDefault();

    X_ASSERT_NOT_NULL(pFont_);

    pRender_ = pCore_->GetIRender();
    pPrimContext_ = pCore_->Get3DEngine()->getPrimContext(engine::PrimContext::CONSOLE);
    return true;
}

// --------------------

void XConsole::shutDown(void)
{
    X_LOG0("Console", "Shutting Down");

    // check if core failed to init.
    if (pCore_) {
        if (coreEventListernRegd_) {
            pCore_->GetCoreEventDispatcher()->RemoveListener(this);
        }
    //    pCore_->GetHotReloadMan()->addfileType(nullptr, CONFIG_FILE_EXTENSION);
        pCore_->GetILog()->RemoveLogger(&logger_);
    }

    // clear up vars.
    if (varMap_.isNotEmpty()) {

        // we use the value of the last item to move iterator
        for (auto it = varMap_.begin(); it != varMap_.end();) 
        {
            auto* pVar = it->second;
            ++it;

            X_DELETE(pVar, &varArena_);
        }

        varMap_.clear();
    }

    inputBuffer_.clear();
    refString_.clear();
    cmdHistory_.clear();
}

void XConsole::freeRenderResources(void)
{
    pRender_ = nullptr;
}

void XConsole::saveChangedVars(void)
{
    if (varMap_.isEmpty()) {
        X_WARNING("Console", "Skipping saving of modified vars. no registered vars.");
        return;
    }

    X_LOG0("Console", "Saving moified vars");

    core::XFileScoped file;

    core::Path<char> userConfigPath("config/");
    userConfigPath.append(USER_CFG_FILE_NAME);

    // we need to handle the case where a modified var is in the config
    // but we are shutting down before that var has been registered again.
    // so it won't get saved out into the new one.
    // so i'm going to parse the existing config and keep any var sets that are for vars that don't currently exist.

    core::Array<char> buf(arena_);
    core::ArrayGrowMultiply<core::StringRange<char>> keep(arena_);

    if (gEnv->pFileSys->fileExists(userConfigPath, core::VirtualDirectory::SAVE)) {
        if (file.openFile(userConfigPath, FileFlag::READ | FileFlag::SHARE, core::VirtualDirectory::SAVE)) {
            const auto size = safe_static_cast<size_t>(file.remainingBytes());

            if (size > 0) {
                buf.resize(size);
                if (file.read(buf.data(), size) != size) {
                    X_ERROR("Console", "Failed to read exsisiting config file data");
                }
                else {
                    core::StringTokenizer<char> tokenizer(buf.begin(), buf.end(), '\n');
                    core::StringRange<char> line(nullptr, nullptr);

                    // we save this file so it should only have 'seta' in but lets not error if something else.
                    while (tokenizer.extractToken(line)) {
                        core::StringTokenizer<char> lineTokenizer(line.getStart(), line.getEnd(), ' ');
                        core::StringRange<char> token(nullptr, nullptr);

                        if (lineTokenizer.extractToken(token) && core::strUtil::IsEqual(token.getStart(), token.getEnd(), "seta")) {
                            // get the name.
                            if (lineTokenizer.extractToken(token)) {
                                // work out if we have this var.
                                core::StackString256 name(token.getStart(), token.getEnd());

                                ICVar* pVar = getCVar(core::string_view(name));
                                if (!pVar) {
                                    keep.push_back(line);
                                }
                            }
                        }
                    }
                }
            }

            file.close();
        }
    }

    core::ByteStream stream(arena_);
    stream.reserve(4096);
    stream.write("// auto generated\n", sizeof("// auto generated\n") - 1);

    for (auto& k : keep) {
        stream.write(k.getStart(), k.getLength());
        stream.write('\n');
    }

    core::ICVar::StrBuf strBuf;

    for (auto& it : varMap_) {
        ICVar* pVar = it.second;
        ICVar::FlagType flags = pVar->GetFlags();

        // we always save 'ARCHIVE' and only save 'SAVE_IF_CHANGED' if 'MODIFIED'
        bool save = (flags.IsSet(VarFlag::SAVE_IF_CHANGED) && flags.IsSet(VarFlag::MODIFIED)) || flags.IsSet(VarFlag::ARCHIVE);

        if (save) {
            // save out name + value.
            auto name = pVar->GetName();
            const char* pValue = pVar->GetString(strBuf);
            
            stream.write("seta ", 5);
            stream.write(name.data(), name.length());
            stream.write(' ');
            stream.write(pValue, core::strUtil::strlen(pValue));
            stream.write('\n');
        }
    }

    // want to open flag in the save game folder.
    if (!file.openFile(userConfigPath, FileFlag::WRITE | FileFlag::RECREATE, core::VirtualDirectory::SAVE)) {
        X_ERROR("Console", "Failed to open file for saving modifed vars");
        return;
    }

    if (file.write(stream.data(), stream.size()) != stream.size()) {
        X_ERROR("Console", "Failed to write modifed vars data");
    }

    file.close();
}


void XConsole::dispatchRepeateInputEvents(core::FrameTimeData& time)
{
    // we must be open to accept input.
    // cancel any repeat events when we close.
    if (!isVisible()) {
        repeatEvent_.keyId = input::KeyId::UNKNOWN;
        return;
    }

    if (repeatEvent_.keyId != input::KeyId::UNKNOWN) {
        // we want to be able to de increment the time.
        repeatEventTimer_ -= time.unscaledDeltas[ITimer::Timer::UI];

        if (repeatEventTimer_.GetValue() < 0) {
            processInput(repeatEvent_);

            repeatEventTimer_ = repeatEventInterval_;
        }
    }
}


void XConsole::runCmds(void)
{
    while (pendingCmdsQueue_.isNotEmpty()) {
        executeStringInternal(pendingCmdsQueue_.peek());
        pendingCmdsQueue_.pop();
    }
}

void XConsole::draw(core::FrameTimeData& time)
{
    cursor_.curTime += time.unscaledDeltas[ITimer::Timer::UI];

    if (cursor_.curTime > cursor_.displayTime) {
        cursor_.draw = !cursor_.draw;   // toggle it
        cursor_.curTime = TimeVal(0ll); // reset
    }

    drawBuffer();
}

bool XConsole::onInputEvent(const input::InputEvent& event)
{
    if (event.action == input::InputState::CHAR) {
        return handleInputChar(event);
    }

    return handleInput(event);
}


ICVar* XConsole::registerString(core::string_view name, core::string_view value,
    VarFlags Flags, core::string_view desc)
{
    ICVar* pCVar = getCVar(name);
    if (pCVar) {
        return pCVar;
    }

    if (Flags.IsSet(VarFlag::CPY_NAME)) {
        pCVar = X_NEW(CVarString<CVarBaseHeap>, &varArena_, "CVarString<H>")(this, name, value, Flags, desc);
    }
    else {
        pCVar = X_NEW(CVarString<CVarBaseConst>, &varArena_, "CVarString")(this, name, value, Flags, desc);
    }

    registerVar(pCVar);
    return pCVar;
}

ICVar* XConsole::registerInt(core::string_view name, int Value, int Min,
    int Max, VarFlags Flags, core::string_view desc)
{
    ICVar* pCVar = getCVarForRegistration(name);
    if (pCVar) {
        return pCVar;
    }

    pCVar = X_NEW(CVarInt<CVarBaseConst>, &varArena_, "CVarInt")(this, name, Value, Min, Max, Flags, desc);
    registerVar(pCVar);
    return pCVar;
}

ICVar* XConsole::registerFloat(core::string_view name, float Value, float Min,
    float Max, VarFlags Flags, core::string_view desc)
{
    ICVar* pCVar = getCVarForRegistration(name);
    if (pCVar) {
        return pCVar;
    }

    pCVar = X_NEW(CVarFloat<CVarBaseConst>, &varArena_, "CVarFloat")(this, name, Value, Min, Max, Flags, desc);
    registerVar(pCVar);
    return pCVar;
}

ICVar* XConsole::registerRef(core::string_view name, float* src, float defaultvalue,
    float Min, float Max, VarFlags flags, core::string_view desc)
{
    X_ASSERT_NOT_NULL(src);

    ICVar* pCVar = getCVarForRegistration(name);
    if (pCVar) {
        return pCVar;
    }

    *src = defaultvalue;

    pCVar = X_NEW(CVarFloatRef, &varArena_, "CVarRefFloat")(this, name, src, Min, Max, flags, desc);
    registerVar(pCVar);
    return pCVar;
}

ICVar* XConsole::registerRef(core::string_view name, int* src, int defaultvalue,
    int Min, int Max, VarFlags flags, core::string_view desc)
{
    X_ASSERT_NOT_NULL(src);

    ICVar* pCVar = getCVarForRegistration(name);
    if (pCVar) {
        return pCVar;
    }

    *src = defaultvalue;

    pCVar = X_NEW(CVarIntRef, &varArena_, "CVarRefInt")(this, name, src, Min, Max, flags, desc);
    registerVar(pCVar);
    return pCVar;
}

ICVar* XConsole::registerRef(core::string_view name, Color* src, Color defaultvalue,
    VarFlags flags, core::string_view desc)
{
    X_ASSERT_NOT_NULL(src);

    ICVar* pCVar = getCVarForRegistration(name);
    if (pCVar) {
        return pCVar;
    }

    *src = defaultvalue;

    pCVar = X_NEW(CVarColRef, &varArena_, "CVarRefCol")(this, name, src, flags, desc);
    registerVar(pCVar);
    return pCVar;
}

ICVar* XConsole::registerRef(core::string_view name, Vec3f* src, Vec3f defaultvalue,
    VarFlags flags, core::string_view desc)
{
    X_ASSERT_NOT_NULL(src);

    ICVar* pCVar = getCVarForRegistration(name);
    if (pCVar) {
        return pCVar;
    }

    *src = defaultvalue;

    pCVar = X_NEW(CVarVec3Ref, &varArena_, "CVarRefVec3")(this, name, src, flags, desc);
    registerVar(pCVar);
    return pCVar;
}

ICVar* XConsole::getCVar(core::string_view name)
{
    if (auto it = varMap_.find(name); it != varMap_.end()) {
        return it->second;
    }
    return nullptr;
}

void XConsole::unregisterVariable(core::string_view varName)
{
    auto it = varMap_.find(varName);
    if (it == varMap_.end()) {
        X_WARNING("Console", "Failed to find var \"%.*s\" for removal", varName.length(), varName.data());
        return;
    }

    auto* pVar = it->second;

    varMap_.erase(it);
    X_DELETE(pVar, &varArena_);
}

void XConsole::unregisterVariable(ICVar* pCVar)
{
    auto name = pCVar->GetName();

    varMap_.erase(name);
    X_DELETE(pCVar, &varArena_);
}

// Commands :)

void XConsole::registerCommand(core::string_view name, ConsoleCmdFunc func, VarFlags Flags, core::string_view desc)
{
    ConsoleCommand cmd;
    cmd.name.assign(name.data(), name.length());
    cmd.desc.assign(desc.data(), desc.length()); // TODO: do we need a copy?
    cmd.flags = Flags;
    cmd.func = func;

    if (cmdMap_.find(cmd.name) != cmdMap_.end()) {
        X_ERROR("Console", "command already exists: \"%.*s", name.length(), name.data());
        return;
    }

    cmdMap_.emplace(cmd.name, cmd);
}

void XConsole::unRegisterCommand(core::string_view name)
{
    auto it = cmdMap_.find(name);
    if (it != cmdMap_.end()) {
        cmdMap_.erase(it);
    }
}

void XConsole::exec(core::string_view command)
{
    addCmd(command, ExecSource::SYSTEM, false);
}


bool XConsole::loadAndExecConfigFile(core::string_view fileName)
{
    core::Path<char> path;

    path = "config";
    path.ensureSlash();
    path.append(fileName.data(), fileName.length());
    path.setExtension(CONFIG_FILE_EXTENSION);

    X_LOG0("Config", "Loading config: \"%.*s\"", fileName.length(), fileName.data());

    core::XFileScoped file;
    if (!file.openFile(path, FileFlag::READ, core::VirtualDirectory::SAVE)) {
        X_ERROR("Config", "failed to load: \"%s\"", path.c_str());
        return false;
    }

    auto bytes = safe_static_cast<size_t>(file.remainingBytes());
    if (bytes == 0) {
        return true;
    }

    core::Array<char, core::ArrayAlignedAllocator<char>> data(arena_);
    data.getAllocator().setBaseAlignment(16);
    data.resize(bytes + 2);

    if (file.read(data.data(), bytes) != bytes) {
        X_ERROR("Config", "failed to read: \"%s\"", path.c_str());
        return false;
    }

    // 2 bytes at end so the multiline search can be more simple.
    // and not have to worrie about reading out of bounds.
    data[bytes] = '\0';
    data[bytes + 1] = '\0';

    // execute all the data in the file.
    // it's parsed in memory.
    // remove comments here.
    char* begin = data.begin();
    char* end = begin + bytes;
    const char* pComment;

    // we support // and /* */ so loook for a '/'
    while ((pComment = core::strUtil::Find(begin, end, '/')) != nullptr) {
        // wee need atleast 1 more char.
        if (pComment >= (end - 1)) {
            break;
        }

        begin = const_cast<char*>(++pComment);

        if (*begin == '*') {
            begin[-1] = ' ';
            *begin++ = ' ';

            while (*begin != '*' && begin[1] != '/' && begin < end) {
                *begin++ = ' ';
            }
        }
        else if (*begin == '/') {
            // signle line.
            begin[-1] = ' ';
            *begin++ = ' ';

            while (*begin != '\n' && begin < end) {
                *begin++ = ' ';
            }
        }
        else {
            ++begin;
        }
    }

    configExec(data.begin(), data.begin() + bytes);
    return true;
}


void XConsole::addLineToLog(const char* pStr, uint32_t length)
{
    X_UNUSED(length);

    decltype(logLock_)::ScopedLock lock(logLock_);

    consoleLog_.emplace(pStr, length);

    const int32_t bufferSize = console_buffer_size;

    if (safe_static_cast<int32_t>(consoleLog_.size()) > bufferSize) {
        consoleLog_.pop();

        const auto noneScroll = maxVisibleLogLines();

        // move scroll wheel with the moving items?
        if (scrollPos_ > 0 && scrollPos_ < (safe_static_cast<std::remove_const<decltype(noneScroll)>::type>(consoleLog_.size()) - noneScroll)) {
            scrollPos_++;
        }
    }
    else {
        if (scrollPos_ > 0) {
            scrollPos_++;
        }
    }
}

int32_t XConsole::getLineCount(void) const
{
    return safe_static_cast<int32_t>(consoleLog_.size());
}

// -------------------------------------------------

ICVar* XConsole::getCVarForRegistration(core::string_view name)
{
    if (auto it = varMap_.find(name); it != varMap_.end()) {
        // if you get this warning you need to fix it.
        X_ERROR("Console", "var(%.*s) is already registered", name.length(), name.data());
        return it->second;
    }

    return nullptr;
}

void XConsole::registerVar(ICVar* pCVar)
{
    auto name = pCVar->GetName();

    if (auto it = configCmds_.find(name); it != configCmds_.end()) {
        if (cvarModifyBegin(pCVar, ExecSource::CONFIG)) {
            pCVar->Set(core::string_view(it->second));
        }

        X_LOG2("Console", "Var \"%.*s\" was set by config on registeration", name.length(), name.data());
    }

    if (auto it = varArchive_.find(name); it != varArchive_.end()) {
        if (cvarModifyBegin(pCVar, ExecSource::CONFIG)) { // is this always gonna be config?
            pCVar->Set(core::string_view(it->second));
        }

        // mark as archive.
        pCVar->SetFlags(pCVar->GetFlags() | VarFlag::ARCHIVE);

        X_LOG2("Console", "Var \"%.*s\" was set by seta on registeration", name.length(), name.data());
    }

    varMap_.emplace(pCVar->GetName(), pCVar);
}



void XConsole::displayVarValue(const ICVar* pVar)
{
    if (!pVar) {
        return;
    }

    auto name = pVar->GetName();

    core::ICVar::StrBuf strBuf;
    X_LOG0("Dvar", "^2\"%.*s\"^7 = ^6%s", name.length(), name.data(), pVar->GetString(strBuf));
}

void XConsole::displayVarInfo(const ICVar* pVar, bool fullInfo)
{
    if (!pVar) {
        return;
    }

    ICVar::FlagType::Description dsc;
    core::ICVar::StrBuf strBuf;

    auto name = pVar->GetName();

    if (fullInfo) {
        auto min = pVar->GetMin();
        auto max = pVar->GetMax();
        auto desc = pVar->GetDesc();
        X_LOG0("Dvar", "^2\"%.*s\"^7 = '%s' min: %f max: %f [^1%s^7] Desc: \"%.*s\"", name.length(), name.data(), pVar->GetString(strBuf), min, max,
            pVar->GetFlags().ToString(dsc), desc.length(), desc.data());
    }
    else {
        X_LOG0("Dvar", "^2\"%.*s\"^7 = %s [^1%s^7]", name.length(), name.data(), pVar->GetString(strBuf),
            pVar->GetFlags().ToString(dsc));
    }
}

// -------------------------

X_INLINE void XConsole::addCmd(core::string_view command, ExecSource::Enum src, bool silent)
{
    pendingCmdsQueue_.emplace(string(command.data(), command.length()), src, silent);
}

void XConsole::addCmd(string&& command, ExecSource::Enum src, bool silent)
{
    pendingCmdsQueue_.emplace(std::move(command), src, silent);
}


void XConsole::executeStringInternal(const ExecCommand& cmd)
{
    ConsoleCommandArgs::CommandNameStr name;
    ConsoleCommandArgs::CommandStr value;
    
    CommandParser parser(cmd.command.begin(), cmd.command.end());

    core::StringRange<char> range(nullptr, nullptr);
    while (parser.extractCommand(range)) {
        // work out name / value
        const char* pPos = range.find('=');

        if (pPos) {
            name.set(range.getStart(), pPos);
        }
        else if ((pPos = range.find(' ')) != nullptr) {
            name.set(range.getStart(), pPos);
        }
        else {
            name.set(range);
        }

        name.trim();
        name.stripColorCodes();

        if (name.isEmpty()) {
            continue;
        }

        // === Check if is a command ===
        if (auto it = cmdMap_.find(name.c_str()); it != cmdMap_.end()) {
            value.set(range);
            value.trim();
            executeCommand((it->second), value);
            continue;
        }

        // === check for var ===
        if (auto it = varMap_.find(name.c_str()); it != varMap_.end()) {
            ICVar* pCVar = it->second;

            if (pPos) // is there a space || = symbol (meaning there is a possible value)
            {
                value.set(pPos + 1, range.getEnd());
                value.trim();

                if (value.isEmpty()) {
                    // no value was given so assume they wanted
                    // to print the value.
                    displayVarInfo(pCVar);
                }
                else if (cvarModifyBegin(pCVar, cmd.source)) {
                    pCVar->Set(core::string_view(value));
                    displayVarValue(pCVar);
                }
            }
            else {
                displayVarInfo(pCVar);
            }

            continue;
        }

        // if this was from config, add it to list.
        // so vars not yet registered can get the value
        if (cmd.source == ExecSource::CONFIG && pPos) {
            value.set(pPos + 1, range.getEnd());
            value.trim();

            auto it = configCmds_.find(name.c_str());
            if (it == configCmds_.end()) {
                configCmds_.emplace(string(name.begin(), name.end()), string(value.begin(), value.end()));
            }
            else {
                it->second = string(value.begin(), value.end());
            }
        }

        if (!cmd.silentMode) {
            if (cmd.source == ExecSource::CONFIG) {
                X_WARNING("Config", "Unknown command/var: %s", name.c_str());
            }
            else {
                X_WARNING("Console", "Unknown command: %s", name.c_str());
            }
        }
    }
}


void XConsole::executeCommand(const ConsoleCommand& cmd, ConsoleCommandArgs::CommandStr& str) const
{
    str.replace('"', '\'');

    if (cmd.flags.IsSet(VarFlag::CHEAT)) {
        X_WARNING("Console", "Cmd(%s) is cheat protected", cmd.name.c_str());
        return;
    }

    if (cmd.func) {
        
        ConsoleCommandArgs::ParseFlags flags;

        if (cmd.flags.IsSet(VarFlag::SINGLE_ARG)) {
            flags.Set(ConsoleCommandArgs::ParseFlag::SINGLE_ARG);
        }

        ConsoleCommandArgs cmdArgs(str, flags);

        if (console_debug) {
            auto cmdStr = cmdArgs.GetArg(0);
            X_LOG0("Console", "Running command \"%.*s\"", cmdStr.length(), cmdStr.data());
        }

        cmd.func.Invoke(&cmdArgs);
    }
}

// ------------------------------------------


void XConsole::configExec(const char* pCommand, const char* pEnd)
{
    // if it's from config, should i limit what commands can be used?
    // for now i'll let any be used

    if (gEnv->isRunning()) {
        addCmd(core::string_view(pCommand, pEnd), ExecSource::CONFIG, false);
    }
    else {
        // we run the command now.
        ExecCommand cmd;
        cmd.command = core::string(pCommand, pEnd);
        cmd.source = ExecSource::CONFIG;
        cmd.silentMode = false;

        executeStringInternal(cmd);
    }
}

// ------------------------------------------


void XConsole::addInputChar(const char c)
{
    const char tidle = '�';

    if (c == '`' || c == tidle) { // sent twice.
        return;
    }

    if (cursorPos_ < safe_static_cast<int32_t, size_t>(inputBuffer_.length())) {
        inputBuffer_.insert(cursorPos_, c);
    }
    else {
        inputBuffer_ = inputBuffer_ + c;
    }

    cursorPos_++;

    //	X_LOG0("Console Buf", "%s (%i)", inputBuffer_.c_str(), cursorPos_);
}

void XConsole::removeInputChar(bool bBackSpace)
{
    if (inputBuffer_.isEmpty()) {
        return;
    }

    if (bBackSpace) {
        if (cursorPos_ > 0) {
            inputBuffer_.erase(cursorPos_ - 1, 1);
            cursorPos_--;
        }
    }
    else {
        // ho ho h.
        X_ASSERT_NOT_IMPLEMENTED();
    }

    //	X_LOG0("Console Buf", "%s (%i)", inputBuffer_.c_str(), cursorPos_);
}

void XConsole::clearInputBuffer(void)
{
    inputBuffer_ = "";
    cursorPos_ = 0;
}

void XConsole::executeInputBuffer(void)
{
    if (inputBuffer_.isEmpty()) {
        return;
    }

    core::string temp = inputBuffer_;
    clearInputBuffer();

    addCmdToHistory(core::string_view(temp.data(), temp.length()));

    addCmd(std::move(temp), ExecSource::CONSOLE, false);
}


bool XConsole::handleInput(const input::InputEvent& event)
{
    // open / close
    const auto keyReleased = event.action == input::InputState::RELEASED;

    if (keyReleased)
    {
        if (event.keyId == input::KeyId::OEM_8)
        {
            bool expand = event.modifiers.IsSet(input::ModifiersMasks::Shift);
            bool visible = isVisible();

            // clear states.
            gEnv->pInput->clearKeyState();

            if (expand) { // shift + ` dose not close anymore just expands.
                showConsole(consoleState::EXPANDED);
            }
            else {
                toggleConsole(false); // toggle it.
            }

            // don't clear if already visible, as we are just expanding.
            if (!visible) {
                clearInputBuffer();
            }

            return true;
        }
        else if (event.keyId == input::KeyId::ESCAPE && isVisible())
        {
            clearInputBuffer();
            showConsole(consoleState::CLOSED);
            return true;
        }
    }

#if 0
    // process key binds when console is hidden
    if (consoleState_ == consoleState::CLOSED)
    {
        const char* pCmdStr = 0;

        if (!event.modifiers.IsAnySet()) {
            pCmdStr = FindBind(event.name);
        }
        else {
            // build the key.
            core::StackString<60> bind_name;

            if (event.modifiers.IsSet(input::ModifiersMasks::Ctrl)) {
                bind_name.append("ctrl ");
            }
            if (event.modifiers.IsSet(input::ModifiersMasks::Shift)) {
                bind_name.append("shift ");
            }
            if (event.modifiers.IsSet(input::ModifiersMasks::Alt)) {
                bind_name.append("alt ");
            }
            if (event.modifiers.IsSet(input::ModifiersMasks::Win)) {
                bind_name.append("win ");
            }

            bind_name.append(event.name);

            pCmdStr = FindBind(bind_name.c_str());
        }

        if (pCmdStr) {
            AddCmd(pCmdStr, ExecSource::CONSOLE, false);
            return true;
        }
    }
#endif

    if (!isVisible()) {
        return false;
    }

    // -- OPEN --
    if (keyReleased) {
        repeatEvent_.keyId = input::KeyId::UNKNOWN;
    }

    if (event.action != input::InputState::PRESSED) 
    {
        if (event.deviceType == input::InputDeviceType::KEYBOARD) {
            return isVisible();
        }

        // eat mouse move?
        // Stops the camera moving around when we have console open.
        if (event.deviceType == input::InputDeviceType::MOUSE) {
            if (event.keyId != input::KeyId::MOUSE_Z) {
                if (console_disable_mouse == 1) // only if expanded
                {
                    return isExpanded();
                }
                if (console_disable_mouse == 2) {
                    return isVisible();
                }

                return false;
            }
        }
        else {
            return false;
        }
    }

    if (event.action == input::InputState::PRESSED) {
        repeatEvent_ = event;
        repeatEventTimer_ = repeatEventInitialDelay_;
    }

    if (isExpanded()) // you can only scroll a expanded console.
    {
        if (event.keyId == input::KeyId::MOUSE_Z) {
            int32_t scaled = static_cast<int32_t>(event.value);
            bool positive = (scaled >= 0);

            scaled /= 20;

            // enuse scaled didn't remove all scrolling
            if (positive && scaled < 1) {
                scaled = 1;
            }
            else if (!positive && scaled > -1) {
                scaled = -1;
            }

            scrollPos_ += scaled;

            validateScrollPos();
            return true;
        }
        else if (event.keyId == input::KeyId::PAGE_UP) {
            pageUp();
        }
        else if (event.keyId == input::KeyId::PAGE_DOWN) {
            pageDown();
        }
    }

  
    if (event.keyId != input::KeyId::TAB) {
        //	ResetAutoCompletion();
    }

    return processInput(event);
}

bool XConsole::handleInputChar(const input::InputEvent& event)
{
    if (!isVisible()) {
        return false;
    }

    repeatEvent_ = event;

    processInput(event);
    return true;
}


bool XConsole::processInput(const input::InputEvent& event)
{
    X_ASSERT(isVisible(), "ProcessInput called when not visible")(isVisible());

    // consume char input.
    if (event.action == input::InputState::CHAR) {

        if (event.keyId == input::KeyId::V && event.modifiers.IsSet(input::ModifiersMasks::Ctrl)) {
            paste();
            return true;
        }

        if (event.keyId == input::KeyId::C && event.modifiers.IsSet(input::ModifiersMasks::Ctrl)) {
            copy();
            return true;
        }

        addInputChar(event.inputchar);
        return true;
    }

    if (event.keyId == input::KeyId::ENTER || event.keyId == input::KeyId::NUMPAD_ENTER) {
        if (autoCompleteIdx_ >= 0) {
            autoCompleteSelect_ = true;
        }
        else {
            executeInputBuffer();
        }
        return true;
    }
    else if (event.keyId == input::KeyId::BACKSPACE || event.keyId == input::KeyId::DELETE) {
        // shift + DEL / BACK fully clears
        if (event.modifiers.IsSet(input::ModifiersMasks::Shift)) {
            clearInputBuffer();
        }
        else {
            removeInputChar(true);
        }
        return true;
    }
    else if (event.keyId == input::KeyId::LEFT_ARROW) {
        if (cursorPos_) { // can we go left?
            cursorPos_--;

            // support moving whole words
            if (event.modifiers.IsSet(input::ModifiersMasks::Ctrl)) {
                while (cursorPos_ && inputBuffer_[cursorPos_] != ' ') {
                    cursorPos_--;
                }
            }

            // disable blinking while moving.
            cursor_.curTime = TimeVal(0ll);
            cursor_.draw = true;

            if (console_cursor_skip_color_codes) {
                // if we are at a number and ^ is before us go back two more.
                if (cursorPos_ >= 1) {
                    const char curChar = inputBuffer_[cursorPos_];

                    if (core::strUtil::IsDigit(curChar)) {
                        const char PreChar = inputBuffer_[cursorPos_ - 1];

                        if (PreChar == '^') {
                            cursorPos_--;
                            if (cursorPos_ > 0) {
                                cursorPos_--;
                            }
                        }
                    }
                }
            }
        }
        return true;
    }
    else if (event.keyId == input::KeyId::RIGHT_ARROW) {
        // are we pre end ?
        if (cursorPos_ < safe_static_cast<int32_t>(inputBuffer_.length())) {
            cursorPos_++;

            // support moving whole words
            if (event.modifiers.IsSet(input::ModifiersMasks::Ctrl)) {
                while (cursorPos_ < safe_static_cast<int32_t>(inputBuffer_.length())
                       && inputBuffer_[cursorPos_] != ' ') {
                    cursorPos_++;
                }
            }

            // disable blinking while moving.
            cursor_.curTime = TimeVal(0ll);
            cursor_.draw = true;

            if (console_cursor_skip_color_codes) {
                uint32_t charsLeft = (safe_static_cast<int32_t>(inputBuffer_.length()) - cursorPos_);
                if (charsLeft >= 2) {
                    const char curChar = inputBuffer_[cursorPos_];
                    const char nextChar = inputBuffer_[cursorPos_ + 1];
                    if (curChar == '^' && core::strUtil::IsDigit(nextChar)) {
                        cursorPos_ += 2;
                    }
                }
            }
        }
        else if (autoCompleteIdx_ >= 0) {
            autoCompleteSelect_ = true;
        }
        return true;
    }
    else if (event.keyId == input::KeyId::HOME) {
        cursorPos_ = 0;
    }
    else if (event.keyId == input::KeyId::END) {
        cursorPos_ = safe_static_cast<int32_t>(inputBuffer_.length());
    }
    else if (event.keyId == input::KeyId::UP_ARROW) {
        if (isAutocompleteVis() && autoCompleteIdx_ >= 0) {
            autoCompleteIdx_ = core::Max(-1, --autoCompleteIdx_);
        }
        else {
            auto historyLine = getHistory(CmdHistory::UP);

            if (!historyLine.empty()) {
                if (console_debug) {
                    X_LOG0("Cmd history", "%.*s", historyLine.length(), historyLine.data());
                }

                inputBuffer_.assign(historyLine.data(), historyLine.length());
                cursorPos_ = safe_static_cast<int32_t>(inputBuffer_.size());
            }
        }
        return true;
    }
    else if (event.keyId == input::KeyId::DOWN_ARROW) {
        bool inHistory = (historyPos_ < static_cast<int32_t>(cmdHistory_.size()));
        bool multiAutoComplete = autoCompleteNum_ > 1;

        if (isAutocompleteVis() && (!inHistory || multiAutoComplete)) {
            autoCompleteIdx_ = core::Min(autoCompleteNum_ - 1, ++autoCompleteIdx_);

            // reset history if we move into autocomplete?
            // i think so..
            resetHistoryPos();
        }
        else {
            auto historyLine = getHistory(CmdHistory::DOWN);

            if (!historyLine.empty()) {
                if (console_debug) {
                    X_LOG0("Cmd history", "%.*s", historyLine.length(), historyLine.data());
                }

                inputBuffer_.assign(historyLine.data(), historyLine.length());
                cursorPos_ = safe_static_cast<int32_t>(inputBuffer_.size());
            }
        }
        return true;
    }

    return true;
}


// ----------------------------


void XConsole::saveCmdHistory(void) const
{
    X_ASSERT_NOT_NULL(gEnv);
    X_ASSERT_NOT_NULL(gEnv->pFileSys);

    core::ByteStream stream(arena_);
    stream.reserve(0x100);

    for (auto& str : cmdHistory_) {
        stream.write(str.c_str(), str.length());
        stream.write("\n", 1);
    }

    FileFlags mode;
    mode.Set(FileFlag::WRITE);
    mode.Set(FileFlag::RECREATE);

    XFileScoped file;
    if (file.openFile(core::Path<>(CMD_HISTORY_FILE_NAME), mode, core::VirtualDirectory::SAVE)) {
        file.write(stream.data(), safe_static_cast<uint32_t>(stream.size()));
    }
}

void XConsole::loadCmdHistory(bool async)
{
    X_ASSERT_NOT_NULL(gEnv);
    X_ASSERT_NOT_NULL(gEnv->pFileSys);

    core::CriticalSection::ScopedLock lock(historyFileLock_);

    if (historyLoadPending_) {
        return;
    }

    FileFlags mode;
    mode.Set(FileFlag::READ);
    mode.Set(FileFlag::SHARE);

    if (async) {
        // load the file async
        historyLoadPending_ = true;

        core::IoRequestOpenRead open;
        open.callback.Bind<XConsole, &XConsole::historyIoRequestCallback>(this);
        open.mode = mode;
        open.path = CMD_HISTORY_FILE_NAME;
        open.arena = arena_;

        gEnv->pFileSys->AddIoRequestToQue(open);
    }
    else {
        XFileMemScoped file;
        if (file.openFile(core::Path<>(CMD_HISTORY_FILE_NAME), mode, core::VirtualDirectory::SAVE)) {
            const char* pBegin = file->getBufferStart();
            const char* pEnd = file->getBufferEnd();

            parseCmdHistory(pBegin, pEnd);
        }
    }
}


void XConsole::historyIoRequestCallback(core::IFileSys& fileSys, const core::IoRequestBase* pRequest,
    core::XFileAsync* pFile, uint32_t bytesTransferred)
{
    X_UNUSED(fileSys, bytesTransferred);

    // history file loaded.
    X_ASSERT(pRequest->getType() == core::IoRequest::OPEN_READ_ALL, "Received unexpected request type")(pRequest->getType());
    const core::IoRequestOpenRead* pOpenRead = static_cast<const IoRequestOpenRead*>(pRequest);

    {
        core::CriticalSection::ScopedLock lock(historyFileLock_);

        historyLoadPending_ = false;

        // if it faild do we care?
        if (!pFile) {
            X_LOG2("Console", "Failed to load history file");
            return;
        }

        // we can't process it on this thread.
        // so store it.
        historyFileBuf_ = core::UniquePointer<const char[]>(pOpenRead->arena, reinterpret_cast<const char*>(pOpenRead->pBuf));
        historyFileSize_ = pOpenRead->dataSize;
    }

    historyFileCond_.NotifyAll();
}

void XConsole::parseCmdHistory(const char* pBegin, const char* pEnd)
{
    core::StringTokenizer<char> tokenizer(pBegin, pEnd, '\n');
    StringRange<char> range(nullptr, nullptr);

    // lets not clear, we can append and just pop off end if too many in total.
    // CmdHistory_.clear();

    while (tokenizer.extractToken(range)) {
        if (range.getLength() > 0) {
            cmdHistory_.emplace(range.getStart(), range.getEnd());

            if (cmdHistory_.size() > MAX_HISTORY_ENTRIES) {
                cmdHistory_.pop();
            }
        }
    }

    resetHistoryPos();
}

void XConsole::addCmdToHistory(core::string_view command)
{
    // make sure it's not same as last command
    if (cmdHistory_.isEmpty() || !cmdHistory_.front().compare(command.begin(), command.length())) {
        cmdHistory_.emplace(command.data(), command.length());
    }

    // limit hte history.
    while (cmdHistory_.size() > MAX_HISTORY_ENTRIES) {
        cmdHistory_.pop();
    }

    resetHistoryPos();

    if (console_save_history) {
        saveCmdHistory();
    }
}

void XConsole::resetHistoryPos(void)
{
    historyPos_ = safe_static_cast<int32_t>(cmdHistory_.size());
}


core::string_view XConsole::getHistory(CmdHistory::Enum direction)
{
    if (cmdHistory_.isEmpty()) {
        return {};
    }

    if (direction == CmdHistory::UP) {

        if (historyPos_ <= 0) {
            return {};
        }

        historyPos_--;

        refString_ = cmdHistory_[historyPos_];
        return core::string_view(refString_);
    }
    else // down
    {
        // are we above base cmd?
        if (historyPos_ < safe_static_cast<int32_t>(cmdHistory_.size()) - 1) {
            historyPos_++;

            // adds a refrence to the string.
            refString_ = cmdHistory_[historyPos_];
            return core::string_view(refString_);
        }
    }

    return {};
}

// --------------------------------

// Binds a cmd to a key
void XConsole::addBind(core::string_view key, core::string_view cmd)
{
    // check for override ?
    auto it = binds_.find(key);
    if (it == binds_.end()) {
        binds_.emplace(core::string(key.data(), key.length()), core::string(cmd.data(), cmd.length()));
        return;
    }

    if (core::strUtil::IsEqual(cmd, core::string_view(it->second))) {
        // bind is same.
        return;
    }

    if (console_debug) {
        X_LOG1("Console", "Overriding bind \"%.*s\" -> %.*s with -> %.*s", 
            key.length(), key.data(), 
            it->second.length(), it->second.data(), 
            cmd.length(), cmd.data());
    }

    it->second = core::string(cmd.data(), cmd.length());
}

// returns the command for a given key
// returns null if no bind found
core::string_view XConsole::findBind(core::string_view key)
{
    if (auto it = binds_.find(key); it != binds_.end()) {
        return core::string_view(it->second);
    }

    return {};
}

// removes all the binds.
void XConsole::clearAllBinds(void)
{
    binds_.clear();
}

void XConsole::listbinds(IKeyBindDumpSink* CallBack)
{
    for (auto bind : binds_) {
        CallBack->OnKeyBindFound(core::string_view(bind.first), core::string_view(bind.second));
    }
}


// --------------------------------------

void XConsole::pageUp(void)
{
    const int32_t visibleNum = maxVisibleLogLines();
    scrollPos_ += visibleNum;

    validateScrollPos();
}

void XConsole::pageDown(void)
{
    const int32_t visibleNum = maxVisibleLogLines();
    scrollPos_ -= visibleNum;

    validateScrollPos();
}

void XConsole::validateScrollPos(void)
{
    if (scrollPos_ < 0) {
        scrollPos_ = 0;
    }
    else {
        int32_t logSize = safe_static_cast<int32_t>(consoleLog_.size());
        const int32_t visibleNum = maxVisibleLogLines();

        logSize -= visibleNum;
        logSize += 2;

        if (scrollPos_ > logSize) {
            scrollPos_ = logSize;
        }
    }
}

// --------------------------------------


void XConsole::resetAutoCompletion(void)
{
    autoCompleteIdx_ = -1;
    autoCompleteNum_ = 0;
    autoCompleteSelect_ = false;
}

int32_t XConsole::maxVisibleLogLines(void) const
{
    const int32_t height = renderRes_.y - 40;

    const float lineSize = (console_output_font_size * console_output_font_line_height);
    return static_cast<int32_t>(height / lineSize);
}

// ------------------------------------------------------


void XConsole::drawBuffer(void)
{
    if (consoleState_ == consoleState::CLOSED) {
        return;
    }

    if (!pPrimContext_) {
        return;
    }

    const Vec2f outputFontSize(static_cast<float>(console_output_font_size), static_cast<float>(console_output_font_size));
    const Vec2f inputFontSize(static_cast<float>(CONSOLE_INPUT_FONT_SIZE), static_cast<float>(CONSOLE_INPUT_FONT_SIZE));

    const Vec2i& res = renderRes_;

    const float xStart = 5.f;
    const float yStart = 35.f;
    const float width = res.x - 10.f;
    const float height = res.y - 40.f;

    font::TextDrawContext ctx;
    ctx.pFont = pFont_;
    ctx.effectId = 0;
    ctx.SetColor(Col_Khaki);
    ctx.SetSize(inputFontSize);

    {
        pPrimContext_->drawQuad(xStart, 5, width, 24, console_input_box_color, console_input_box_color_border);

        if (isExpanded()) {
            pPrimContext_->drawQuad(xStart, yStart, width, height, console_output_box_color, console_output_box_color_border);

            if (console_output_draw_channel) {
                ctx.SetSize(outputFontSize);

                const float32_t channelWidth = pFont_->GetCharWidth(L' ', 15, ctx);

                pPrimContext_->drawQuad(xStart, yStart, channelWidth, height, console_output_box_channel_color);

                ctx.SetSize(inputFontSize);
            }
        }
    }

    {
        const char* pTxt = X_ENGINE_NAME " Engine " X_BUILD_STRING ">";
        const char* pTxtEnd = pTxt + (sizeof(X_ENGINE_NAME " Engine " X_BUILD_STRING ">") - 1);

        Vec2f pos(10, 5);
        Vec2f txtwidth = pFont_->GetTextSize(pTxt, pTxtEnd, ctx);

        ctx.SetEffectId(pFont_->GetEffectId("drop"));

        pPrimContext_->drawText(Vec3f(pos.x, pos.y, 1), ctx, pTxt, pTxtEnd);

        ctx.SetDefaultEffect();
        ctx.SetColor(Col_White);

        pos.x += txtwidth.x;
        pos.x += 2;

        if (cursor_.draw) {
            float Lwidth = pFont_->GetTextSize(inputBuffer_.c_str(), inputBuffer_.c_str() + cursorPos_, ctx).x + 3; // 3px offset from engine txt.

            pPrimContext_->drawText(pos.x + Lwidth, pos.y, ctx, "_");
        }

        // the log.
        if (isExpanded()) {
            ctx.SetSize(outputFontSize);
            ctx.clip.set(0, 30, width, std::numeric_limits<float>::max());
            ctx.flags.Set(font::DrawTextFlag::CLIP);

            float fCharHeight = ctx.GetCharHeight() * console_output_font_line_height;

            float xPos = 8;
            float yPos = (height + yStart) - (fCharHeight + 10); // 15 uints up
            int32_t scroll = 0;

            decltype(logLock_)::ScopedLock lock(logLock_);

            const int32_t num = safe_static_cast<int32_t>(consoleLog_.size());

            for (int32_t i = (num - 1); i >= 0 && yPos >= 30; --i, ++scroll) {
                if (scroll >= scrollPos_) {
                    const auto& text = consoleLog_[i];

                    pPrimContext_->drawText(xPos, yPos, ctx, text.begin(), text.end());
                    yPos -= fCharHeight;
                }
            }

            drawScrollBar();
        }

        // draw the auto complete
        drawInputTxt(pos);
    }
}

void XConsole::drawScrollBar(void)
{
    if (!isExpanded()) {
        return;
    }

    if (pFont_ && pPrimContext_ && pRender_) {
        // oooo shit nuggger wuggger.
        const Vec2i& res = renderRes_;

        const float width = res.x - 10.f;
        const float height = res.y - 40.f;

        const float barWidth = 6.f;
        const float marging = 5.f;
        const float sliderHeight = 20.f;

        float start_x = (width + 5) - (barWidth + marging);
        float start_y = 35.f + marging;
        float barHeight = height - (marging * 2);

        float slider_x = start_x;
        float slider_y = start_y;
        float slider_width = barWidth;
        float slider_height = sliderHeight;

        // work out the possition of slider.
        // we take the height of the bar - slider and divide.
        const int32_t visibleNum = maxVisibleLogLines();
        const int32_t scrollableLines = safe_static_cast<int32_t>(consoleLog_.size()) - (visibleNum + 2);

        float positionPercent = PercentageOf(scrollPos_, scrollableLines) * 0.01f;
        float offset = (barHeight - slider_height) * positionPercent;

        slider_y += (barHeight - slider_height - offset);
        if (slider_y < start_y) {
            slider_y = start_y;
        }

        pPrimContext_->drawQuad(start_x, start_y, barWidth, barHeight, console_output_scroll_bar_color);
        pPrimContext_->drawQuad(slider_x, slider_y, slider_width, slider_height, console_output_scroll_bar_slider_color);
    }
}

void XConsole::drawInputTxt(const Vec2f& start)
{
    const size_t max_auto_complete_results = 32;
    typedef core::FixedArray<AutoResult, max_auto_complete_results> Results;
    Results results;

    Color txtCol = Col_White;
    Vec2f txtPos = start;

    if (pFont_ && pPrimContext_) {
        const char* inputBegin = inputBuffer_.begin();
        const char* inputEnd = inputBuffer_.end();

        if (inputBuffer_.isEmpty()) {
            resetAutoCompletion();
            return;
        }

        // check for vreset
        if (const char* vreset = core::strUtil::Find(inputBegin, inputEnd, "vreset ")) {
            // check if we have atleast 1 char.
            if (inputBuffer_.length() > 7) {
                inputBegin = core::Min(vreset + 7, inputEnd); // cap search.
                txtCol = console_cmd_color;
            }
        }

        // check for spaces.
        if (const char* space = core::strUtil::Find(inputBegin, inputEnd, ' ')) {
            //	if (space == (inputEnd -1))
            inputEnd = space; // cap search. (-1 will be safe since must be 1 char in string)
        }

        const size_t inputLen = inputEnd - inputBegin;
        if (inputLen == 0) {
            return;
        }

        typedef core::traits::Function<bool(const char*, const char*,
            const char*, const char*)>
            MyComparisionFunc;

        MyComparisionFunc::Pointer pComparison = core::strUtil::IsEqual;
        if (!console_case_sensitive) {
            pComparison = core::strUtil::IsEqualCaseInsen;
        }

        // try find and cmd's / dvars that match the current input.
        auto it = varMap_.begin();

        for (; it != varMap_.end(); ++it) {
            auto name = it->second->GetName();
            
            // if var name shorter than search leave it !
            if (name.length() < inputLen) {
                continue;
            }

            // TODO: make results take string_view
            X_ASSERT(*name.end() == '\0', "Name needs to be nullterm until this is code is updated")();

            // we search same length.
            if (pComparison(name.data(), name.data() + inputLen, inputBegin, inputEnd)) {
                results.emplace_back(name.data(), it->second, nullptr);
            }

            if (results.size() == results.capacity()) {
                break;
            }
        }

        if (results.size() < results.capacity()) {
            // do the commands baby!
            auto cmdIt = cmdMap_.begin();
            for (; cmdIt != cmdMap_.end(); ++cmdIt) {
                const char* pName = cmdIt->second.name.c_str();
                size_t nameLen = cmdIt->second.name.length();

                // if cmd name shorter than search leave it !
                if (nameLen < inputLen) {
                    continue;
                }

                // we search same length.
                if (pComparison(pName, pName + inputLen, inputBegin, inputEnd)) {
                    results.emplace_back(pName, nullptr, &cmdIt->second);
                }

                if (results.size() == results.capacity()) {
                    break;
                }
            }
        }

        // sort them?
        std::sort(results.begin(), results.end());

        // Font contex
        font::TextDrawContext ctx;
        ctx.pFont = pFont_;
        ctx.effectId = 0;
        ctx.SetSize(Vec2f(14, 14));

        // Autocomplete
        if (autoCompleteNum_ != safe_static_cast<int, size_t>(results.size())) {
            autoCompleteIdx_ = -1;
        }

        autoCompleteNum_ = safe_static_cast<int, size_t>(results.size());
        autoCompleteIdx_ = core::Min(autoCompleteIdx_, autoCompleteNum_ - 1);
        autoCompleteSelect_ = autoCompleteIdx_ >= 0 ? autoCompleteSelect_ : false;

        if (autoCompleteSelect_) {
            if (inputBuffer_.find("vreset ")) {
                inputBuffer_ = "vreset ";
                inputBuffer_ += results[autoCompleteIdx_].pName;
            }
            else {
                inputBuffer_ = results[autoCompleteIdx_].pName;
            }
            //	if (results[autoCompleteIdx_].var) // for var only?
            inputBuffer_ += ' '; // add a space
            cursorPos_ = safe_static_cast<int32, size_t>(inputBuffer_.length());
            autoCompleteIdx_ = -1;
            autoCompleteSelect_ = false;
        }
        // ~AutoComplete

        if (!results.isEmpty()) {
            // calculate a size.
            float fCharHeight = 1.1f * ctx.GetCharHeight();
            float xpos = start.x;
            float ypos = start.y + 30;
            float width = 200; // min width
            float height = 5;
        resultsChanged:
            bool isSingleVar = results.size() == 1 && results[0].var;
            bool isSingleCmd = results.size() == 1 && !results[0].var;

            Color col = console_input_box_color;
            col.a = 1.f;

            if (isSingleCmd) {
                // if what they have entered fully matches a cmd,
                // change the txt color to darkblue.
                // we need to check if it's only a substring match currently.
                core::StackString<128> temp(inputBegin, inputEnd);
                const char* fullName = results[0].pName;

                temp.trim();

                if (temp.isEqual(fullName)) {
                    txtCol = console_cmd_color;
                }
            }
            else if (results.size() > 1) {
                // if there is a space after the cmd's name.
                // we check if the input has a complete match
                // to any of the results.
                // if so only show that.
                string::const_str pos = inputBuffer_.find(' ');
                if (pos) // != string::npos) //== (inputBuffer_.length() - 1))
                {
                    Results::const_iterator resIt;
                    core::StackString<128> temp(inputBuffer_.begin(), pos);

                    resIt = results.begin();
                    for (; resIt != results.end(); ++resIt) {
                        if (core::strUtil::IsEqual(temp.c_str(), resIt->pName)) {
                            // ok remove / add.
                            AutoResult res = *resIt;

                            results.clear();
                            results.append(res);
                            goto resultsChanged;
                        }
                    }
                }
            }

            if (isSingleVar) {
                core::StackString<128> nameStr;
                core::StackString<32> defaultStr;
                core::StackString<128> value; // split the value and name for easy alignment.
                core::StackString<128> defaultValue;
                core::StackString<512> description;
                core::StackString<256> domain;

                ICVar* pCvar = results[0].var;
                ICVar::FlagType flags = pCvar->GetFlags();

                const float nameValueSpacing = 15.f;
                const float colorBoxWidth = 40.f;
                const float colorBoxPadding = 3.f;
                float xposValueOffset = 0.f;
                bool isColorVar = pCvar->GetType() == VarFlag::COLOR;
                // display more info for a single var.
                // varname: value
                // default: default_value
                // ------------------
                // Description.
                // Possible Values;

                {
                    auto name = pCvar->GetName();
                    nameStr.append(name.data(), name.length());
                }
                defaultStr.append("	default");

                {
                    ICVar::StrBuf strBuf;

                    value.appendFmt("%s", pCvar->GetString(strBuf));
                    defaultValue.appendFmt("%s", pCvar->GetDefaultStr(strBuf));
                }

                {
                    auto desc = pCvar->GetDesc();
                    description.append(desc.data(), desc.length());
                }

                if (pCvar->GetType() == VarFlag::INT) {
                    if (flags.IsSet(VarFlag::BITFIELD)) {
                        // need to work out all bits that are set in max.

                        auto intToalphaBits = [](int32_t val, core::StackString<48>& strOut) {
                            strOut.clear();
                            for (uint32_t b = 1; b < sizeof(val) * 8; b++) {
                                if (core::bitUtil::IsBitSet(val, b)) {
                                    strOut.append(core::bitUtil::BitToAlphaChar(b), 1);
                                }
                            }
                        };

                        core::StackString<48> allBitsStr;
                        core::StackString<48> valueBitsStr;
                        core::StackString<48> defaultBitsStr;

                        intToalphaBits(pCvar->GetMaxInt(), allBitsStr);
                        intToalphaBits(pCvar->GetInteger(), valueBitsStr);
                        intToalphaBits(pCvar->GetDefaultInt(), defaultBitsStr);

                        value.appendFmt(" (%s)", valueBitsStr.c_str());
                        defaultValue.appendFmt(" (%s)", defaultBitsStr.c_str());
                        domain.appendFmt("Domain is bitfield of the following: '%s' commands: b, b+, b-, b^", allBitsStr.c_str());
                    }
                    else {
                        domain.appendFmt("Domain is any interger between: %d and %d",
                            pCvar->GetMinInt(),
                            pCvar->GetMaxInt());
                    }
                }
                else if (pCvar->GetType() == VarFlag::FLOAT) {
                    domain.appendFmt("Domain is any real number between: %g and %g",
                        pCvar->GetMin(),
                        pCvar->GetMax());
                }
                else if (pCvar->GetType() == VarFlag::COLOR) {
                    domain.appendFmt("Domain is 1 or 4 real numbers between: 0.0 and 1.0");
                }

                height = fCharHeight * 2.5f;
                width = core::Max(
                    width,
                    pFont_->GetTextSize(nameStr.begin(), nameStr.end(), ctx).x,
                    pFont_->GetTextSize(defaultStr.begin(), defaultStr.end(), ctx).x);

                width += nameValueSpacing; // name - value spacing.
                xposValueOffset = width;
                width += core::Max(
                    pFont_->GetTextSize(value.begin(), value.end(), ctx).x,
                    pFont_->GetTextSize(defaultValue.begin(), defaultValue.end(), ctx).x);

                float box2Offset = 5;
                float height2 = fCharHeight * (domain.isEmpty() ? 1.5f : 2.5f);
                float width2 = core::Max(
                    width,
                    pFont_->GetTextSize(description.begin(), description.end(), ctx).x,
                    pFont_->GetTextSize(domain.begin(), domain.end(), ctx).x);

                width += 15;  // add a few pixels.
                width2 += 15; // add a few pixels.

                if (isColorVar) {
                    width += colorBoxWidth + colorBoxPadding;
                }

                // Draw the boxes
                pPrimContext_->drawQuad(xpos, ypos, width, height, col, console_input_box_color_border);
                pPrimContext_->drawQuad(xpos, ypos + height + box2Offset, width2, height2, col, console_input_box_color_border);

                if (isColorVar) {
                    float colxpos = xpos + width - (colorBoxPadding + colorBoxWidth);
                    float colypos = ypos + (colorBoxPadding * 2.f);
                    float colHeight = fCharHeight - colorBoxPadding * 2;

                    // draw the colors :D !
                    CVarColRef* PColVar = static_cast<CVarColRef*>(pCvar);

                    pPrimContext_->drawQuad(colxpos, colypos, colorBoxWidth, colHeight, PColVar->GetColor(), Col_Black);

                    // 2nd box.
                    colypos += fCharHeight;

                    pPrimContext_->drawQuad(colxpos, colypos, colorBoxWidth, colHeight, PColVar->GetDefaultColor(), Col_Black);

                    // How about a preview of the new color?
                    string::const_str pos = inputBuffer_.find(nameStr.c_str());
                    if (pos) {
                        //	static core::StackString<64> lastValue;
                        core::StackString<64> colorStr(&pos[nameStr.length()],
                            inputBuffer_.end());

                        colorStr.trim();

                        // save a lex if string is the same.
                        // slap myself, then it's not drawn lol.
                        // add some checks if still needs drawing if the lex time is a issue.
                        if (!colorStr.isEmpty()) // && colorStr != lastValue)
                        {
                            //	lastValue = colorStr;

                            // parse it.
                            Color previewCol;
                            if (CVarColRef::ColorFromString(core::string_view(colorStr), previewCol)) {
                                // draw a box on the end cus i'm a goat.
                                pPrimContext_->drawQuad(xpos + width + 5, ypos, height, height, previewCol, Col_Black);
                            }
                        }
                    }
                }

                // draw da text baby!
                xpos += 5;

                ctx.SetColor(Col_Whitesmoke);
                pPrimContext_->drawText(xpos, ypos, ctx, nameStr.begin(), nameStr.end());
                pPrimContext_->drawText(xpos + xposValueOffset, ypos, ctx, value.begin(), value.end());

                ctx.SetColor(Col_Darkgray);
                ypos += fCharHeight;
                pPrimContext_->drawText(xpos, ypos, ctx, defaultStr.begin(), defaultStr.end());
                pPrimContext_->drawText(xpos + xposValueOffset, ypos, ctx, defaultValue.begin(), defaultValue.end());

                ypos += fCharHeight * 1.5f;
                ypos += box2Offset;

                ctx.SetColor(Col_Darkgray);
                pPrimContext_->drawText(xpos, ypos, ctx, description.begin(), description.end());

                if (!domain.isEmpty()) {
                    ypos += fCharHeight;
                    pPrimContext_->drawText(xpos, ypos, ctx, domain.begin(), domain.end());
                }
            }
            else if (isSingleCmd) {
                AutoResult& result = *results.begin();
                const core::string& descStr = result.pCmd->desc;

                const float box2Offset = 5.f;
                const float descWidth = core::Max(width, pFont_->GetTextSize(descStr.begin(), descStr.end(), ctx).x) + 10.f;

                width = core::Max(width, pFont_->GetTextSize(result.pName, result.pName + core::strUtil::strlen(result.pName), ctx).x);
                width += 10; // add a few pixels.
                height += fCharHeight;

                pPrimContext_->drawQuad(xpos, ypos, width, height, col, console_output_box_color_border);
                pPrimContext_->drawQuad(xpos, ypos + height + box2Offset, descWidth, height, col, console_input_box_color_border);

                xpos += 5.f;

                // cmd color
                ctx.SetColor(console_cmd_color);
                pPrimContext_->drawText(xpos, ypos, ctx, result.pName);

                ypos += fCharHeight;
                ypos += 5.f;
                ypos += box2Offset;

                ctx.SetColor(Col_Whitesmoke);
                pPrimContext_->drawText(xpos, ypos, ctx, descStr.begin(), descStr.end());
            }
            else {
                Results::const_iterator resIt;

                resIt = results.begin();
                for (; resIt != results.end(); ++resIt) {
                    width = core::Max(width, pFont_->GetTextSize(resIt->pName, resIt->pName + core::strUtil::strlen(resIt->pName), ctx).x);
                    height += fCharHeight;
                }

                width += 10; // add a few pixels.
                             //		height += 5; // add a few pixels.

                pPrimContext_->drawQuad(xpos, ypos, width, height, col, console_output_box_color_border);

                xpos += 5;

                resIt = results.begin();

                for (int idx = 0; resIt != results.end(); ++resIt, idx++) {
                    if (autoCompleteIdx_ >= 0 && autoCompleteIdx_ == idx) {
                        ctx.SetColor(Col_Darkturquoise);
                    }
                    else if (resIt->var) {
                        ctx.SetColor(Col_Whitesmoke);
                    }
                    else {
                        ctx.SetColor(console_cmd_color);
                    }

                    pPrimContext_->drawText(xpos, ypos, ctx, resIt->pName);

                    ypos += fCharHeight;
                }
            }
        }

        // the input
        if (!inputBuffer_.isEmpty()) {
            const Vec2i& res = renderRes_;

            const float width = res.x - 10.f;
            const Vec2f inputFontSize(static_cast<float>(CONSOLE_INPUT_FONT_SIZE), static_cast<float>(CONSOLE_INPUT_FONT_SIZE));

            ctx.SetSize(inputFontSize);
            ctx.SetColor(txtCol);
            ctx.flags.Set(font::DrawTextFlag::CLIP);
            ctx.clip.set(0, 0, width, std::numeric_limits<float>::max());

            core::string::const_str space = inputBuffer_.find(' ');
            if (space) {
                core::StackString<128> temp(inputBuffer_.begin(), space);

                // preserve any colors.
                core::StackString<128> temp2;

                if (const char* pCol = temp.find('^')) {
                    if (core::strUtil::IsDigit(pCol[1])) {
                        temp2.appendFmt("^%c", pCol[1]);
                    }
                }

                //	temp2.append(&inputBuffer_[space]);
                temp2.append(space);

                pPrimContext_->drawText(txtPos.x, txtPos.y, ctx, temp.begin(), temp.end());
                ctx.SetColor(Col_White);
                txtPos.x += pFont_->GetTextSize(temp.begin(), temp.end(), ctx).x;
                pPrimContext_->drawText(txtPos.x, txtPos.y, ctx, temp2.begin(), temp2.end());
            }
            else {
                pPrimContext_->drawText(txtPos.x, txtPos.y, ctx, inputBuffer_.begin(), inputBuffer_.end());
            }
        }
    }
}

// ----------------------

void XConsole::copy(void)
{
    core::clipboard::setText(inputBuffer_.begin(), inputBuffer_.end());
}

void XConsole::paste(void)
{
    core::clipboard::ClipBoardBuffer buffer;
    const char* pTxt = core::clipboard::getText(buffer);

    if (pTxt) {
        // insert it at current pos.
        inputBuffer_.insert(cursorPos_, pTxt);
        // add to length
        cursorPos_ += safe_static_cast<int32_t, size_t>(core::strUtil::strlen(pTxt));
    }
    else {
        X_LOG1("Console", "Failed to paste text to console");
    }
}

// ----------------------

void XConsole::OnCoreEvent(const CoreEventData& ed)
{
    if (ed.event == CoreEvent::RENDER_RES_CHANGED) {
        renderRes_.x = static_cast<int32_t>(ed.renderRes.width);
        renderRes_.y = static_cast<int32_t>(ed.renderRes.height);
    }
}

// ----------------------

void XConsole::listCommands(core::string_view searchPattern)
{
    core::Array<const ConsoleCommand*> sorted_cmds(arena_);
    sorted_cmds.setGranularity(cmdMap_.size());

    for (const auto& it : cmdMap_) {
        const ConsoleCommand& cmd = it.second;

        if (searchPattern.empty() || strUtil::WildCompare(searchPattern, core::string_view(cmd.name))) {
            sorted_cmds.append(&cmd);
        }
    }

    sortCmdsByName(sorted_cmds);

    X_LOG0("Console", "------------ ^8Commands(%" PRIuS ")^7 ------------", sorted_cmds.size());

    ConsoleCommand::VarFlags::Description dsc;
    for (const auto* pCmd : sorted_cmds) {
        X_LOG0("Command", "^2\"%s\"^7 [^1%s^7] Desc: \"%s\"", pCmd->name.c_str(), pCmd->flags.ToString(dsc), pCmd->desc.c_str());
    }

    X_LOG0("Console", "------------ ^8Commands End^7 ------------");
}

void XConsole::listVariables(core::string_view searchPattern)
{
    core::Array<const ICVar*> sorted_vars(arena_);
    sorted_vars.setGranularity(varMap_.size());

    for (const auto& it : varMap_) {
        ICVar* var = it.second;

        if (searchPattern.empty() || strUtil::WildCompare(searchPattern, core::string_view(var->GetName()))) {
            sorted_vars.emplace_back(var);
        }
    }

    sortVarsByName(sorted_vars);

    X_LOG0("Console", "-------------- ^8Vars(%" PRIuS ")^7 --------------", sorted_vars.size());

    ICVar::FlagType::Description dsc;
    for (const auto& var : sorted_vars) {
        auto name = var->GetName();
        auto desc = var->GetDesc();
        X_LOG0("Dvar", "^2\"%.*s\"^7 [^1%s^7] Desc: \"%.*s\"", 
            name.length(), name.data(), var->GetFlags().ToString(dsc), desc.length(), desc.data());
    }

    X_LOG0("Console", "-------------- ^8Vars End^7 --------------");
}

void XConsole::listVariablesValues(core::string_view searchPattern)
{
    core::Array<const ICVar*> sorted_vars(arena_);
    sorted_vars.setGranularity(varMap_.size());

    for (const auto& it : varMap_) {
        ICVar* var = it.second;

        if (searchPattern.empty() || strUtil::WildCompare(searchPattern, core::string_view(var->GetName()))) {
            sorted_vars.emplace_back(var);
        }
    }

    sortVarsByName(sorted_vars);

    X_LOG0("Console", "-------------- ^8Vars(%" PRIuS ")^7 --------------", sorted_vars.size());

    for (const auto* pVar : sorted_vars) {
        displayVarValue(pVar);
    }

    X_LOG0("Console", "-------------- ^8Vars End^7 --------------");
}


// ==================================================================

void XConsole::Command_Exec(IConsoleCmdArgs* pCmd)
{
    if (pCmd->GetArgCount() != 2) {
        X_WARNING("Console", "exec <filename>");
        return;
    }

    auto filename = pCmd->GetArg(1);

    loadAndExecConfigFile(filename);
}

void XConsole::Command_History(IConsoleCmdArgs* pCmd)
{
    core::string_view searchPattern;
    if (pCmd->GetArgCount() == 2) {
        searchPattern = pCmd->GetArg(1);
    }

    X_LOG0("Console", "-------------- ^8History^7 ---------------");
    X_LOG_BULLET;

    int32_t idx = 0;
    for (const auto& history : cmdHistory_) {
        if (searchPattern.empty() || core::strUtil::WildCompare(searchPattern, core::string_view(history))) {
            X_LOG0("Console", "> %" PRIi32 " %s", idx, history.c_str());
        }

        ++idx;
    }

    X_LOG0("Console", "------------ ^8History End^7 -------------");
}

void XConsole::Command_Help(IConsoleCmdArgs* pCmd)
{
    X_UNUSED(pCmd);

    X_LOG0("Console", "------- ^8Help^7 -------");
    X_LOG_BULLET;
    X_LOG0("Console", "listcmds: lists avaliable commands");
    X_LOG0("Console", "listdvars: lists dvars");
    X_LOG0("Console", "listbinds: lists all the bind");
}

void XConsole::Command_ListCmd(IConsoleCmdArgs* pCmd)
{
    core::string_view searchPattern;

    if (pCmd->GetArgCount() >= 2) {
        searchPattern = pCmd->GetArg(1);
    }

    listCommands(searchPattern);
}

void XConsole::Command_ListDvars(IConsoleCmdArgs* pCmd)
{
    core::string_view searchPattern;

    if (pCmd->GetArgCount() >= 2) {
        searchPattern = pCmd->GetArg(1);
    }

    listVariables(searchPattern);
}

void XConsole::Command_ListDvarsValues(IConsoleCmdArgs* pCmd)
{
    core::string_view searchPattern;

    if (pCmd->GetArgCount() >= 2) {
        searchPattern = pCmd->GetArg(1);
    }

    listVariablesValues(searchPattern);
}

void XConsole::Command_Exit(IConsoleCmdArgs* pCmd)
{
    X_UNUSED(pCmd);

    // we want to exit I guess.
    // dose this check even make sense?
    // it might for dedicated server.
    if (gEnv && gEnv->pCore && gEnv->pCore->GetGameWindow()) {
        gEnv->pCore->GetGameWindow()->Close();
    }
    else {
        X_ERROR("Cmd", "Failed to exit game");
    }
}

void XConsole::Command_Echo(IConsoleCmdArgs* pCmd)
{
    if (pCmd->GetArgCount() != 2) {
        X_WARNING("Console", "echo <text>");
        return;
    }

    auto str = pCmd->GetArg(1);
    X_LOG0("echo", "%.*s", str.length(), str.data());
}

void XConsole::Command_VarReset(IConsoleCmdArgs* pCmd)
{
    if (pCmd->GetArgCount() != 2) {
        X_WARNING("Console", "vreset <var_name>");
        return;
    }

    // find the var
    auto str = pCmd->GetArg(1);

    ICVar* pCvar = getCVar(str);
    if (!pCvar) {
        X_ERROR("Console", "var with name \"%.*s\" not found", str.length(), str.data());
        return;
    }

    pCvar->Reset();

    displayVarValue(pCvar);
}

void XConsole::Command_VarDescribe(IConsoleCmdArgs* pCmd)
{
    if (pCmd->GetArgCount() != 2) {
        X_WARNING("Console", "vdesc <var_name>");
        return;
    }

    // find the var
    auto str = pCmd->GetArg(1);

    ICVar* pCvar = getCVar(str);
    if (!pCvar) {
        X_ERROR("Console", "var with name \"%.*s\" not found", str.length(), str.data());
        return;
    }

    displayVarInfo(pCvar, true);
}

void XConsole::Command_Bind(IConsoleCmdArgs* pCmd)
{
    size_t Num = pCmd->GetArgCount();

    if (Num < 3) {
        X_WARNING("Console", "bind <key_combo> '<cmd>'");
        return;
    }

    auto cmdStr = pCmd->GetArgToEnd(2);

    addBind(pCmd->GetArg(1), cmdStr);
}

void XConsole::Command_BindsClear(IConsoleCmdArgs* pCmd)
{
    X_UNUSED(pCmd);

    clearAllBinds();
}

void XConsole::Command_BindsList(IConsoleCmdArgs* pCmd)
{
    X_UNUSED(pCmd);

    struct PrintBinds : public IKeyBindDumpSink
    {
        virtual void OnKeyBindFound(core::string_view bind, core::string_view command) X_FINAL
        {
            X_LOG0("Console", "Key: %.*s Cmd: \"%.*s\"", bind.length(), bind.data(), command.length(), command.data());
        }
    };

    PrintBinds print;

    X_LOG0("Console", "--------------- ^8Binds^7 ----------------");

    listbinds(&print);

    X_LOG0("Console", "------------- ^8Binds End^7 --------------");
}

void XConsole::Command_SetVarArchive(IConsoleCmdArgs* Cmd)
{
    size_t Num = Cmd->GetArgCount();

    if (Num != 3 && Num != 5 && Num != 6) {
        X_WARNING("Console", "seta <var> <val>");
        return;
    }

    // TODO: improve logic IConsoleCmdArgs should just expose a way to geta view of all data after a given arg idx.
    auto varName = Cmd->GetArg(1);

    if (ICVar* pCBar = getCVar(varName)) {
        VarFlag::Enum type = pCBar->GetType();
        if (type == VarFlag::COLOR || type == VarFlag::VECTOR) {
            auto argStr = Cmd->GetArgToEnd(2);
            pCBar->Set(argStr);
        }
        else {
            if (Num != 3) {
                X_WARNING("Console", "seta <var> <val>");
                return;
            }

            auto argStr = Cmd->GetArg(2);
            pCBar->Set(argStr);
        }

        pCBar->SetFlags(pCBar->GetFlags() | VarFlag::ARCHIVE);
    }
    else {

        auto argStr = Cmd->GetArgToEnd(2);

        // we just add it to config cmd map
        auto it = varArchive_.find(varName);
        if (it == varArchive_.end()) {
            varArchive_.emplace(core::string(varName.data(), varName.length()), core::string(argStr.data(), argStr.length()));
        }
        else {
            it->second = core::string(argStr.data(), argStr.length());
        }
    }
}

void XConsole::Command_ConsoleShow(IConsoleCmdArgs* pCmd)
{
    X_UNUSED(pCmd);

    showConsole(XConsole::consoleState::OPEN);
}

void XConsole::Command_ConsoleHide(IConsoleCmdArgs* pCmd)
{
    X_UNUSED(pCmd);

    showConsole(XConsole::consoleState::CLOSED);
}

void XConsole::Command_ConsoleToggle(IConsoleCmdArgs* pCmd)
{
    X_UNUSED(pCmd);

    toggleConsole(false);
}

void XConsole::Command_SaveModifiedVars(IConsoleCmdArgs* pCmd)
{
    X_UNUSED(pCmd);

    saveChangedVars();
}

X_NAMESPACE_END
