#include "stdafx.h"
#include "MenuManger.h"

#include "ScriptBinds.h"

#include <Assets\AssetLoader.h>

#include <IConsole.h>
#include <IRender.h>

#include "Material\MaterialManager.h"

X_NAMESPACE_BEGIN(engine)

namespace gui
{

    XMenuManager::XMenuManager(core::MemoryArenaBase* arena, XMaterialManager* pMatMan) :
        arena_(arena),
        pMatMan_(pMatMan),
        pScriptSys_(nullptr),
        pAssetLoader_(nullptr),
        menuHandler_(ctx_),
        menus_(arena, sizeof(MenuResource), X_ALIGN_OF(MenuResource), "MenuPool")
    {

    }

    XMenuManager::~XMenuManager()
    {

    }

    bool XMenuManager::init(void)
    {
        X_LOG0("MenuManager", "Starting GUI System");

        ADD_COMMAND_MEMBER("listUi", this, XMenuManager, &XMenuManager::Cmd_ListUis, core::VarFlags::SYSTEM, "List the loaded ui");

        pScriptSys_ = gEnv->pScriptSys;

        pAssetLoader_ = gEnv->pCore->GetAssetLoader();
        pAssetLoader_->registerAssetType(assetDb::AssetType::MENU, this, MENU_FILE_EXTENSION);

        pScriptBinds_ = X_NEW(ScriptBinds_Menu, arena_, "MenuScriptBinds")(pScriptSys_, ctx_, menuHandler_);
        pScriptBinds_->bind();

        auto* pCursor = gEngEnv.pMaterialMan_->loadMaterial("ui/cursor");

        menuHandler_.init(pCursor);

        return true;
    }

    void XMenuManager::shutdown(void)
    {
        X_LOG0("MenuManager", "Shutting Down");

        if (pScriptBinds_) {
            X_DELETE(pScriptBinds_, arena_);
        }

        freeDangling();
    }

    IMenuHandler* XMenuManager::getMenuHandler(void)
    {
        return &menuHandler_;
    }

    IMenu* XMenuManager::loadMenu(const char* pName)
    {
        X_ASSERT(core::strUtil::FileExtension(pName) == nullptr, "Extension not allowed")(pName);

        core::string name(pName);
        core::ScopedLock<MenuContainer::ThreadPolicy> lock(menus_.getThreadPolicy());

        auto* pMenu = menus_.findAsset(name);
        if (pMenu) {
            // inc ref count.
            pMenu->addReference();
            return pMenu;
        }

        // we create a model and give it back
        pMenu = menus_.createAsset(name, name);

        addLoadRequest(pMenu);

        return pMenu;
    }

    IMenu* XMenuManager::findMenu(const char* pName)
    {
        core::string name(pName);
        core::ScopedLock<MenuContainer::ThreadPolicy> lock(menus_.getThreadPolicy());

        auto* pMenu = menus_.findAsset(name);
        if (pMenu) {
            return pMenu;
        }

        X_WARNING("MenuManager", "Failed to find menu: \"%s\"", pName);
        return nullptr;
    }

    void XMenuManager::releaseGui(IMenu* pMenu)
    {
        MenuResource* pMenuRes = static_cast<MenuResource*>(pMenu);
        if (pMenuRes->removeReference() == 0) {

            menus_.releaseAsset(pMenuRes);
        }
    }

    bool XMenuManager::waitForLoad(IMenu* pIGui)
    {
        auto* pGui = static_cast<Menu*>(pIGui);
        if (pGui->getStatus() == core::LoadStatus::Complete) {
            return true;
        }

        return pAssetLoader_->waitForLoad(pGui);
    }

    void XMenuManager::freeDangling(void)
    {
        {
            core::ScopedLock<MenuContainer::ThreadPolicy> lock(menus_.getThreadPolicy());

            // any left?
            for (const auto& m : menus_) {
                auto* pMenuRes = m.second;
                const auto& name = pMenuRes->getName();
                X_WARNING("MenuManager", "\"%s\" was not deleted. refs: %" PRIi32, name.c_str(), pMenuRes->getRefCount());
            }
        }

        menus_.free();
    }


    void XMenuManager::addLoadRequest(MenuResource* pMenu)
    {
        pAssetLoader_->addLoadRequest(pMenu);
    }

    void XMenuManager::onLoadRequestFail(core::AssetBase* pAsset)
    {
        X_UNUSED(pAsset);
    }

    bool XMenuManager::processData(core::AssetBase* pAsset, core::UniquePointer<char[]> data, uint32_t dataSize)
    {
        auto* pMenu = static_cast<Menu*>(pAsset);

        return pMenu->processData(std::move(data), dataSize);
    }

    bool XMenuManager::onFileChanged(const core::AssetName& assetName, const core::string& name)
    {
        X_UNUSED(assetName);

        core::ScopedLock<MenuContainer::ThreadPolicy> lock(menus_.getThreadPolicy());

        auto* pMenuRes = menus_.findAsset(name);
        if (!pMenuRes) {
            X_LOG1("MenuManager", "Not reloading \"%s\" it's not currently used", name.c_str());
            return false;
        }

        X_LOG0("MenuManager", "Reloading: %s", name.c_str());

        pAssetLoader_->reload(pMenuRes, core::ReloadFlag::Beginframe);
        return true;
    }

    void XMenuManager::listGuis(const char* pWildcardSearch) const
    {
        X_UNUSED(pWildcardSearch);
    }

    void XMenuManager::Cmd_ListUis(core::IConsoleCmdArgs* pArgs)
    {
        // we support wildcards
        const char* pSearchString = nullptr;
        if (pArgs->GetArgCount() > 1) {
            pSearchString = pArgs->GetArg(1);
        }

        listGuis(pSearchString);
    }

} // namespace gui

X_NAMESPACE_END