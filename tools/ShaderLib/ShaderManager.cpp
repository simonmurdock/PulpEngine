#include "stdafx.h"
#include "ShaderManager.h"
#include "ShaderPermatation.h"

#include <Assets\AssetLoader.h>

#include <Hashing\crc32.h>
#include <Hashing\sha1.h>
#include <String\Lexer.h>
#include <String\StringHash.h>
#include <String\AssetName.h>

#include <Util\UniquePointer.h>
#include <Threading\JobSystem2.h>
#include <Threading\UniqueLock.h>
#include <Threading\ScopedLock.h>

#include <IConsole.h>
#include <IFileSys.h>

#include "ShaderSourceTypes.h"
#include "HWShader.h"
#include "ShaderUtil.h"

X_NAMESPACE_BEGIN(render)

namespace shader
{
    namespace
    {
        template<typename TFlags>
        void AppendFlagTillEqual(const Flags<TFlags>& srcflags, Flags<TFlags>& dest)
        {
            if (srcflags.IsAnySet() && srcflags.ToInt() != dest.ToInt()) {
                for (size_t i = 0; i < TFlags::FLAG_COUNT; i++) {
                    TFlags::Enum flag = static_cast<TFlags::Enum>(1 << i);
                    if (srcflags.IsSet(flag) && !dest.IsSet(flag)) {
                        dest.Set(flag);
                        return;
                    }
                }
            }
        }

    } // namespace

    XShaderManager::XShaderManager(core::MemoryArenaBase* arena) :
        arena_(arena),
        pAssetLoader_(nullptr),
        shaderBin_(arena),
        sourceBin_(arena),
        hwShaders_(arena, sizeof(HWShaderResource), X_ALIGN_OF(HWShaderResource), "HWShaderPool"),
        permHeap_(
            core::bitUtil::RoundUpToMultiple<size_t>(
                PoolArena::getMemoryRequirement(sizeof(ShaderPermatation)) * MAX_SHADER_PERMS,
                core::VirtualMem::GetPageSize())),
        permAllocator_(permHeap_.start(), permHeap_.end(),
            PoolArena::getMemoryRequirement(sizeof(SourceFile)),
            PoolArena::getMemoryAlignmentRequirement(X_ALIGN_OF(ShaderPermatation)),
            PoolArena::getMemoryOffsetRequirement()),
        permArena_(&permAllocator_, "PermPool")
    {
        arena->addChildArena(&permArena_);
    }

    XShaderManager::~XShaderManager()
    {
        arena_->removeChildArena(&permArena_);
    }

    void XShaderManager::registerVars(void)
    {
        vars_.RegisterVars();
    }

    void XShaderManager::registerCmds(void)
    {
        ADD_COMMAND_MEMBER("shaderListHw", this, XShaderManager, &XShaderManager::Cmd_ListHWShaders, core::VarFlag::SYSTEM,
            "lists the loaded shaders");
        ADD_COMMAND_MEMBER("shaderListsourcebin", this, XShaderManager, &XShaderManager::Cmd_ListShaderSources, core::VarFlag::SYSTEM,
            "lists the loaded shaders sources");

        // alternate names
        ADD_COMMAND_MEMBER("listHWShaders", this, XShaderManager, &XShaderManager::Cmd_ListHWShaders, core::VarFlag::SYSTEM,
            "lists the loaded shaders");
        ADD_COMMAND_MEMBER("listShaderSource", this, XShaderManager, &XShaderManager::Cmd_ListShaderSources, core::VarFlag::SYSTEM,
            "lists the loaded shaders sources");
    }

    bool XShaderManager::init(void)
    {
        X_ASSERT_NOT_NULL(gEnv);
        X_LOG1("ShadersManager", "Starting");
        X_PROFILE_NO_HISTORY_BEGIN("ShaderMan", core::profiler::SubSys::RENDER);

        pAssetLoader_ = gEnv->pCore->GetAssetLoader();
        pAssetLoader_->registerAssetType(assetDb::AssetType::SHADER, this, COMPILED_SHADER_FILE_EXTENSION);

        return true;
    }

    bool XShaderManager::shutDown(void)
    {
        X_LOG1("ShadersManager", "Shutting Down");
        X_ASSERT_NOT_NULL(gEnv);

        freeSourcebin();
        freeHwShaders();

        return true;
    }

    XHWShader* XShaderManager::createHWShader(shader::ShaderType::Enum type, const core::string& entry, const core::string& customDefines,
        const core::string& sourceFile, shader::PermatationFlags permFlags, render::shader::VertexFormat::Enum vertFmt)
    {
        ILFlags ilflags = Util::IlFlagsForVertexFormat(vertFmt);

        return hwForName(type, entry, customDefines, sourceFile, permFlags, ilflags);
    }

    XHWShader* XShaderManager::createHWShader(shader::ShaderType::Enum type, const core::string& entry,
        const core::string& customDefines, const core::string& sourceFile,
        shader::PermatationFlags permFlags, ILFlags ILFlags)
    {
        return hwForName(type, entry, customDefines, sourceFile, permFlags, ILFlags);
    }

    void XShaderManager::releaseHWShader(XHWShader* pHWSHader)
    {
        HWShaderResource* pHWRes = static_cast<HWShaderResource*>(pHWSHader);

        if (pHWRes->removeReference() == 0) {
            hwShaders_.releaseAsset(pHWRes);
        }
    }

    bool XShaderManager::compileShader(XHWShader* pHWShader, CompileFlags flags)
    {
        const auto status = pHWShader->getStatus();
        if (status == ShaderStatus::Ready) {
            return true;
        }

        if (status == ShaderStatus::FailedToCompile) {
            X_ERROR("ShadersManager", "can't compile shader, it previously failed to compile");
            return false;
        }

        if (pHWShader->getLock().TryEnter()) {
            // we adopt the lock we have from tryEnter this is not a re-lock.
            core::UniqueLock<XHWShader::LockType> lock(pHWShader->getLock(), core::adopt_lock);

            // If shader reload is enable 'try' to load the source if can't try from bin.
            // if shader reload disabled try bin first without passing source, fallback to source.

#if !X_ENABLE_RENDER_SHADER_RELOAD

            // just try load the shader and return
            if (shaderBin_.loadShader(pHWShader, nullptr)) {
                X_ASSERT(pHWShader->getStatus() == ShaderStatus::Ready, "Shader from cache is not read to rock")();
                return true;
            }

#endif // !X_ENABLE_RENDER_SHADER_RELOAD

            // now load the source file.
            auto* pSource = sourceBin_.loadRawSourceFile(pHWShader->getShaderSource(), false);
            if (!pSource) 
            {
                if (vars_.useCache() && shaderBin_.loadShader(pHWShader, nullptr)) {
                    X_ASSERT(pHWShader->getStatus() == ShaderStatus::Ready, "Shader from cache is not read to rock")();
                    return true;
                }

                X_ERROR("ShadersManager", "Failed to get source for compiling: \"%s\"", pHWShader->getShaderSource().c_str());
                return false;
            }

            // try load it from cache.
            if (vars_.useCache() && shaderBin_.loadShader(pHWShader, pSource)) {
                X_ASSERT(pHWShader->getStatus() == ShaderStatus::Ready, "Shader from cache is not read to rock")();
                return true;
            }

            core::Array<uint8_t> source(arena_);
            if (!sourceBin_.getMergedSource(pSource, source)) {
                X_ERROR("ShadersManager", "Failed to get source for compiling: \"%s\"", pHWShader->getName().c_str());
                return false;
            }

            const bool compiled = pHWShader->compile(source, flags);

            if (!compiled) {
                auto errInfo = pHWShader->getErrorInfo();
                if (errInfo.lineNo >= 0) {
                    auto info = sourceBin_.getSourceInfoForMergedLine(pSource, errInfo.lineNo);

                    X_ERROR("ShadersManager", "Failed to compile shader. Error in \"%s\" line: %" PRIi32 " col: %" PRIi32 "-%" PRIi32,
                        info.name.c_str(), info.line, errInfo.colBegin, errInfo.colEnd);

                    X_BREAKPOINT;
                }
                else {
                    X_ERROR("ShadersManager", "Failed to compile shader");
                }
            }

            if (vars_.writeMergedSource()) {
                saveMergedSource(pHWShader, std::move(source));
            }
            
            if (!compiled) {
                return false;
            }

            // save it
            if (vars_.writeCompiledShaders()) {
                if (!shaderBin_.saveShader(pHWShader, pSource)) {
                    X_WARNING("ShadersManager", "Failed to save shader to bin: \"%s\"", pHWShader->getName().c_str());
                }
            }
        }
        else {
            // another thread is compiling the shader.
            // we wait for it, maybe run some jobs while we wait?
            int32_t backoff = 0;
            while (1) {
                const auto status = pHWShader->getStatus();
                if (status == ShaderStatus::Ready) {
                    break;
                }
                else if (status == ShaderStatus::FailedToCompile) {
                    X_ERROR("ShadersManager", "Failed to compile shader");
                    return false;
                }
                else {
                    core::Thread::backOff(backoff++);
                }
            }
        }

        return true;
    }

    void XShaderManager::saveMergedSource(const XHWShader* pHWShader, ShaderSourceByteArr&& source)
    {
        X_ASSERT(arena_->isThreadSafe(), "Arena must be thread safe, to dispatch background write")();

        // just dispatch a async write request.
        // the source memory will get cleaned up for us once complete.
        core::IoRequestOpenWrite req(std::move(source));
        getShaderCompileSrc(pHWShader, req.path);
        req.callback.Bind<XShaderManager, &XShaderManager::IoCallback>(this);

        gEnv->pFileSys->AddIoRequestToQue(req);
    }

    void XShaderManager::compileShader_job(CompileJobInfo* pJobInfo, uint32_t num)
    {
        for (uint32_t i = 0; i < num; i++) {
            auto& info = pJobInfo[i];
            info.result = compileShader(info.pHWShader, info.flags);
        }
    }

    shader::IShaderPermatation* XShaderManager::createPermatation(const shader::ShaderStagesArr& stagesIn)
    {
        // ok so for now when we create a permatation we also require all the shaders to be compiled.
        // we return null if a shader fails to compile.

        static_assert(decltype(permArena_)::IS_THREAD_SAFE, "PermArena must be thread safe");
        core::UniquePointer<ShaderPermatation> pPerm = core::makeUnique<ShaderPermatation>(&permArena_, stagesIn, arena_);

        if (!compilePermatation_Int(pPerm.get())) {
            return nullptr;
        }

        // we still need to make cb links and get ilFmt even if all the hardware shaders are compiled.
        pPerm->generateMeta();

        return pPerm.release();
    }

    bool XShaderManager::compilePermatation(shader::IShaderPermatation* pIPerm)
    {
        ShaderPermatation* pPerm = static_cast<ShaderPermatation*>(pIPerm);

        if (pPerm->isCompiled()) {
            return true;
        }

        if (!compilePermatation_Int(pPerm)) {
            return false;
        }

        pPerm->generateMeta();
        return true;
    }

    bool XShaderManager::compilePermatation_Int(ShaderPermatation* pPerm)
    {
        CompileFlags flags;

#if X_DEBUG
		flags = COMPILE_DEBUG_FLAGS;
#else
        flags = COMPILE_RELEASE_FLAGS;
#endif // !X_DEBUG

        // we want to compile this then work out the cbuffer links.
        const auto& stages = pPerm->getStages();
        core::FixedArray<CompileJobInfo, ShaderStage::FLAGS_COUNT> jobInfo;

        // dispatch jobs, to compile all da stages yo.
        for (auto* pHWShader : stages) {
            if (!pHWShader || pHWShader->isValid()) {
                continue;
            }

            jobInfo.emplace_back(pHWShader, flags);
        }

        core::Delegate<void(CompileJobInfo*, uint32_t)> del;
        del.Bind<XShaderManager, &XShaderManager::compileShader_job>(this);

        auto* pJob = gEnv->pJobSys->parallel_for_member<XShaderManager>(
            del,
            jobInfo.data(),
            static_cast<uint32_t>(jobInfo.size()),
            core::V2::CountSplitter(1)
                JOB_SYS_SUB_ARG(core::profiler::SubSys::RENDER));

        gEnv->pJobSys->Run(pJob);
        gEnv->pJobSys->Wait(pJob);

        for (const auto& info : jobInfo) {
            if (!info.result) {
                X_ERROR("ShadersManager", "Failed to compile shader for permatation");
                return false;
            }
        }

        return true;
    }

    void XShaderManager::releaseShaderPermatation(shader::IShaderPermatation* pIPerm)
    {
        // term the perm!
        ShaderPermatation* pPerm = static_cast<ShaderPermatation*>(pIPerm);

        const auto& stages = pPerm->getStages();
        for (auto* pHWShader : stages) {
            if (!pHWShader) {
                continue;
            }

            releaseHWShader(pHWShader);
        }

        X_DELETE(pPerm, &permArena_);
    }

    void XShaderManager::getShaderCompileSrc(const XHWShader* pShader, core::Path<char>& srcOut)
    {
        const auto& name = pShader->getName();
        srcOut.clear();
        srcOut.appendFmt("shaders/temp/%.*s.fxcb.%s", name.length(), name.data(), SOURCE_FILE_EXTENSION);

        // make sure the directory is created.
        gEnv->pFileSys->createDirectoryTree(srcOut, core::VirtualDirectory::BASE);
    }

    IShaderSource* XShaderManager::sourceforName(const core::string& name)
    {
        return sourceBin_.loadRawSourceFile(name, false);
    }

    XHWShader* XShaderManager::hwForName(ShaderType::Enum type,
        const core::string& entry, const core::string& customDefines, const core::string& sourceFile,
        const shader::PermatationFlags permFlags, ILFlags ILFlags)
    {
        core::Hash::SHA1 sha1;
        core::Hash::SHA1Digest::String sha1Buf;

        if (entry.isNotEmpty()) {
            sha1.update(entry.data(), entry.length());
        }
        else {
            const char* pEntry = DEFAULT_SHADER_ENTRY[type];
            sha1.update(pEntry);
        }

        sha1.update(customDefines.begin(), customDefines.length());
        sha1.update(permFlags);
        sha1.update(ILFlags);
        sha1.update(type); // include this?
        auto digest = sha1.finalize();

        core::StackString256 name;
        name.append(sourceFile.data(), sourceFile.length());
        name.append('@', 1);
        name.append(digest.ToString(sha1Buf));

#if X_DEBUG
        X_LOG1("Shader", "Load: \"%.*s\"", name.length(), name.data());
#endif // !X_DEBUG

        core::string_view nameView(name);

        // we must have a single lock during the find and create otherwise we have a race.
        core::ScopedLock<HWShaderContainer::ThreadPolicy> lock(hwShaders_.getThreadPolicy());

        HWShaderResource* pHWShaderRes = hwShaders_.findAsset(nameView);
        if (pHWShaderRes) {
            pHWShaderRes->addReference();
            return pHWShaderRes;
        }

        pHWShaderRes = hwShaders_.createAsset(
            nameView,
            vars_,
            type,
            entry,
            customDefines,
            sourceFile,
            permFlags,
            ILFlags,
            arena_);

        return pHWShaderRes;
    }

    void XShaderManager::freeSourcebin(void)
    {
        sourceBin_.free();
    }

    void XShaderManager::freeHwShaders(void)
    {
        hwShaders_.free();
    }

    void XShaderManager::listHWShaders(core::string_view searchPattern)
    {
        core::ScopedLock<HWShaderContainer::ThreadPolicy> lock(hwShaders_.getThreadPolicy());

        core::Array<HWShaderContainer::Resource*> sorted_shaders(arena_);
        hwShaders_.getSortedAssertList(sorted_shaders, searchPattern);


        X_LOG0("Shader", "------------- ^8Shaders(%" PRIuS ")^7 -------------", hwShaders_.size());
        for (const auto& it : sorted_shaders) {
            const auto* pShader = it;

            const char* pFmt = "Name: ^2\"%s\"^7 Status: ^2%s^7 Type: ^2%s^7 IL: ^2%s^7 NumInst: ^2%" PRIi32
                               "^7 Refs: ^2%" PRIi32
                               "^7 CompCnt: ^2%" PRIi32;

            int32_t compileCount = -1;

#if X_ENABLE_RENDER_SHADER_RELOAD
            compileCount = pShader->getCompileCount();
#endif // !X_ENABLE_RENDER_SHADER_RELOAD

            // little ugly as can't #if def inside macro call.
            X_LOG0("Shader", pFmt,
                pShader->getName().c_str(),
                ShaderStatus::ToString(pShader->getStatus()),
                ShaderType::ToString(pShader->getType()),
                InputLayoutFormat::ToString(pShader->getILFormat()),
                pShader->getNumInstructions(),
                pShader->getRefCount(),
                compileCount);
        }
        X_LOG0("Shader", "------------ ^8Shaders End^7 -------------");
    }

    void XShaderManager::listShaderSources(core::string_view searchPattern)
    {
        sourceBin_.listShaderSources(searchPattern);
    }

    void XShaderManager::IoCallback(core::IFileSys&, const core::IoRequestBase*, core::XFileAsync*, uint32_t)
    {
    }


    void XShaderManager::onLoadRequestFail(core::AssetBase* pAsset)
    {
        X_UNUSED(pAsset);
        X_ASSERT_UNREACHABLE();
    }

    bool XShaderManager::processData(core::AssetBase* pAsset, core::UniquePointer<char[]> data, uint32_t dataSize)
    {
        X_UNUSED(pAsset, data, dataSize);
        X_ASSERT_UNREACHABLE();
        return true;
    }

    bool XShaderManager::onFileChanged(const core::AssetName& assetName, const core::string& name)
    {
        // SOURCE_FILE_EXTENSION
        // SOURCE_INCLUDE_FILE_EXTENSION
        // COMPILED_SHADER_FILE_EXTENSION

        const char* pExt = assetName.extension(false);
        if (pExt) {
            // this is just a cache update ignore this.
            if (core::strUtil::IsEqualCaseInsen(pExt, COMPILED_SHADER_FILE_EXTENSION)) {
                return false;
            }

            // ignore .fxcb.hlsl which are merged sources saved out for debuggin.
            if (assetName.findCaseInsen(SOURCE_MERGED_FILE_EXTENSION)) {
                return false;
            }

            if (!sourceBin_.sourceForName(name)) {
                X_WARNING("Shader", "Skipping reload of \"%s\" it's not currently used", name.c_str());
                return false;
            }

            // force a reload of the source.
            const auto* pSource = sourceBin_.loadRawSourceFile(name, true);
            if (!pSource) {
                return false;
            }

            /*
            we can have dozens of hardware shaders that use this source file.
            each hardware shader holds a pointer to it's shader source.
            so we could just iterate all hardware shaders and invalid any using it.

            might want todo something different if end up with loads of shaders
            like storing refrence lists on the shaders.
            */

            core::ScopedLock<HWShaderContainer::ThreadPolicy> lock(hwShaders_.getThreadPolicy());

            for (const auto& shader : hwShaders_) {
                if (shader.second->getShaderSource() == pSource->getName()) {
                    X_LOG0("Shader", "Reloading: %s", shader.second->getName().c_str());
                    shader.second->markStale();
                }
            }
        }

        return true;
    }


    void XShaderManager::Cmd_ListHWShaders(core::IConsoleCmdArgs* pArgs)
    {
        core::string_view searchPattern;

        if (pArgs->GetArgCount() >= 2) {
            searchPattern = pArgs->GetArg(1);
        }

        listHWShaders(searchPattern);
    }

    void XShaderManager::Cmd_ListShaderSources(core::IConsoleCmdArgs* pArgs)
    {
        core::string_view searchPattern;

        if (pArgs->GetArgCount() >= 2) {
            searchPattern = pArgs->GetArg(1);
        }

        listShaderSources(searchPattern);
    }

} // namespace shader

X_NAMESPACE_END