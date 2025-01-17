#pragma once

#include "ScriptBinds.h"

#include "Vars\ScriptVars.h"

#include <Memory\ThreadPolicies\MultiThreadPolicy.h>
#include <Memory\AllocationPolicies\PoolAllocator.h>
#include <Memory\AllocationPolicies\GrowingPoolAllocator.h>
#include <Memory\AllocationPolicies\GrowingBlockAllocator.h>
#include <Memory\HeapArea.h>

#include <Assets\AssertContainer.h>
#include <Assets\AssetBase.h>
#include <Assets\AssetLoader.h>

#include <Threading\Signal.h>

#include <IFileSys.h>

X_NAMESPACE_DECLARE(core,
                    namespace V2 {
                        struct Job;
                        class JobSystem;
                    }

                    struct IConsoleCmdArgs;

                    class AssetLoader);

X_NAMESPACE_BEGIN(script)

class Script;

class XScriptSys : public IScriptSys
    , private core::IAssetLoadSink
{
    typedef core::AssetContainer<Script, SCRIPT_MAX_LOADED, core::SingleThreadPolicy> ScriptContainer;
    typedef ScriptContainer::Resource ScriptResource;
    typedef ScriptContainer AssetContainer;

    typedef core::Fifo<Script*> ScriptQueue;

    typedef core::Array<XScriptTable*> ScriptTableArr;
    typedef core::Array<XScriptBinds*> ScriptBindsArr;
    typedef core::Array<XScriptBindsBase*> ScriptBindsBaseArr;
    typedef core::Array<IScript*> ScriptArr;

    typedef core::MemoryArena<
        core::GrowingPoolAllocator,
        core::SingleThreadPolicy,
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
        PoolArena;

public:
    XScriptSys(core::MemoryArenaBase* arena);
    // IScriptSys
    ~XScriptSys() X_FINAL;

    void registerVars(void) X_FINAL;
    void registerCmds(void) X_FINAL;

    bool init(void) X_FINAL;
    void shutDown(void) X_FINAL;
    void release(void) X_FINAL;

    bool asyncInitFinalize(void) X_FINAL;

    void update(const core::FrameData& frame) X_FINAL;

    IScript* findScript(core::string_view name) X_FINAL;
    IScript* loadScript(core::string_view name) X_FINAL;

    bool waitForLoad(core::AssetBase* pScript) X_FINAL;
    bool waitForLoad(IScript* pScript) X_FINAL; // returns true if load succeed.


    int32_t onInclude(IFunctionHandler* pH);
    bool executeBuffer(const char* pBegin, const char* pEnd, const char* pDesc);

    bool runScriptInSandbox(const char* pBegin, const char* pEnd) const X_FINAL;
    bool loadBufferToTable(const char* pBegin, const char* pEnd, const char* pDesc, IScriptTable* pTable) X_FINAL;

    ScriptFunctionHandle getFunctionPtr(const char* pFuncName) X_FINAL;
    ScriptFunctionHandle getFunctionPtr(const char* pTableName, const char* pFuncName) X_FINAL;
    bool compareFuncRef(ScriptFunctionHandle f1, ScriptFunctionHandle f2) X_FINAL;
    void releaseFunc(ScriptFunctionHandle f) X_FINAL;

    IScriptTable* createTable(bool empty = false) X_FINAL;
    IScriptBinds* createScriptBind(void) X_FINAL;

    void setGlobalValue(const char* pKey, const ScriptValue& val) X_FINAL;
    bool getGlobalValue(const char* pKey, ScriptValue& any) X_FINAL;

    bool call(ScriptFunctionHandle f) X_FINAL;
    bool call(ScriptFunctionHandle f, const ScriptValue& value) X_FINAL;

    bool beginCall(ScriptFunctionHandle f) X_FINAL;
    bool beginCall(const char* pFunName) X_FINAL;
    bool beginCall(const char* pTableName, const char* pFunName) X_FINAL;
    bool beginCall(IScriptTable* pTable, const char* pFunName) X_FINAL;

    void pushCallArg(const ScriptValue& any) X_FINAL;

    bool endCall(int32_t numReturnValues);
    bool endCall(void) X_FINAL;
    bool endCall(ScriptValue& value) X_FINAL;

    IScriptTable* createUserData(void* ptr, size_t size) X_FINAL;

    void onScriptError(const char* fmt, ...) X_FINAL;
    void logCallStack(void) X_FINAL;

    // ~IScriptSys
public:
    X_INLINE lua_State* getLuaState(void);

    XScriptTable* allocTable(void);
    void freeTable(XScriptTable* pTable);

    bool getRecursiveAny(IScriptTable* pTable, const core::StackString<256>& key, ScriptValue& any);

    bool popAny(ScriptValue& var);
    void pushAny(const ScriptValue& var);
    void pushVec3(const Vec3f& vec);
    void pushTable(IScriptTable* pTable);
    bool toVec3(Vec3f& vec, int index);
    bool toAny(ScriptValue& var, int index);

    static bool toAny(lua_State* L, ScriptValue& var, int index);

private:
    void freeDangling(void);
    void releaseScript(Script* pScript);

    void processLoadedScritpts(void);
    bool processLoadedScript(Script* pScript);
    void proicessMissingIncludes(Script* pScript, const char* pBegin, const char* pEnd);
    bool processPreload(uint8_t* pData, size_t length);

    void addLoadRequest(ScriptResource* pScript);
    void onLoadRequestFail(core::AssetBase* pAsset) X_FINAL;
    bool processData(core::AssetBase* pAsset, core::UniquePointer<char[]> data, uint32_t dataSize) X_FINAL;
    bool onFileChanged(const core::AssetName& assetName, const core::string& name) X_FINAL;

    void IoRequestCallback(core::IFileSys&, const core::IoRequestBase*, core::XFileAsync*, uint32_t);

private:
    void listBinds(void) const;
    void listScripts(core::string_view searchPattern) const;

private:
    void listBinds(core::IConsoleCmdArgs* pArgs);
    void listScripts(core::IConsoleCmdArgs* pArgs);
    void dumpState(core::IConsoleCmdArgs* pArgs);

private:
    core::MemoryArenaBase* arena_;
    core::AssetLoader* pAssetLoader_;

    lua_State* L; // hot

    PoolArena::AllocationPolicy poolAllocator_;
    PoolArena poolArena_;

    ScriptTableArr scriptTables_;
    core::CriticalSection cs_;

    lua::RefId errrorHandler_;
    int32_t numCallParams_;

    ScriptVars vars_;

    ScriptBindsArr scriptBinds_;
    ScriptBindsBaseArr baseBinds_;

    ScriptContainer scripts_;

    // loading
    ScriptQueue completedLoads_;

    // preload
    ScriptArr preloads_;
    volatile core::RequestHandle preloadFileReq_;
    core::Signal requestSignal_;
    bool preloadParseFailed_;
};

X_INLINE lua_State* XScriptSys::getLuaState(void)
{
    return L;
}

X_NAMESPACE_END
