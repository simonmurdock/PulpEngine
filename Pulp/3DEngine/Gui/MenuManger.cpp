#include "stdafx.h"
#include "MenuManger.h"

#include "ScriptBinds.h"

#include <Assets\AssetLoader.h>

#include <IConsole.h>
#include <IRender.h>

#include "Material\MaterialManager.h"

X_NAMESPACE_BEGIN(engine)

using namespace core::string_view_literals;

namespace gui
{

    XMenuManager::XMenuManager(core::MemoryArenaBase* arena, XMaterialManager* pMatMan) :
        arena_(arena),
        pMatMan_(pMatMan),
        pScriptSys_(nullptr),
        pAssetLoader_(nullptr),
        menus_(arena, sizeof(MenuResource), X_ALIGN_OF(MenuResource), "MenuPool")
    {

    }

    XMenuManager::~XMenuManager()
    {

    }

    void XMenuManager::registerVars(void)
    {

    }

    void XMenuManager::registerCmds(void)
    {
        ADD_COMMAND_MEMBER("listUi", this, XMenuManager, &XMenuManager::Cmd_ListUis, core::VarFlags::SYSTEM, "List the loaded ui");
    }

    bool XMenuManager::init(void)
    {
        X_LOG0("MenuManager", "Starting GUI System");

        pScriptSys_ = gEnv->pScriptSys;

        pAssetLoader_ = gEnv->pCore->GetAssetLoader();
        pAssetLoader_->registerAssetType(assetDb::AssetType::MENU, this, MENU_FILE_EXTENSION);

        pScriptBinds_ = X_NEW(ScriptBinds_Menu, arena_, "MenuScriptBinds")(pScriptSys_, ctx_);
        pScriptBinds_->bind();

        auto* pCursor = gEngEnv.pMaterialMan_->loadMaterial("ui/cursor"_sv);
        auto* pSpinner = gEngEnv.pMaterialMan_->loadMaterial("ui/spinner"_sv);

        ctx_.init(pCursor, pSpinner);
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

    void XMenuManager::setActiveHandler(MenuHandler* pMenuHandler)
    {
        pScriptBinds_->setActiveHandler(pMenuHandler);
    }

    IMenuHandler* XMenuManager::createMenuHandler(void)
    {
        return X_NEW(MenuHandler, arena_, "MenuHandler")(ctx_, *this);
    }

    void XMenuManager::releaseMenuHandler(IMenuHandler* pHandler)
    {
        X_DELETE(pHandler, arena_);
    }

    IMenu* XMenuManager::loadMenu(core::string_view name)
    {
        X_ASSERT(core::strUtil::FileExtension(name) == nullptr, "Extension not allowed")();

        core::ScopedLock<MenuContainer::ThreadPolicy> lock(menus_.getThreadPolicy());

        auto* pMenu = menus_.findAsset(name);
        if (pMenu) {
            // inc ref count.
            pMenu->addReference();
            return pMenu;
        }

        // we create a model and give it back
        pMenu = menus_.createAsset(name);

        addLoadRequest(pMenu);

        return pMenu;
    }

    IMenu* XMenuManager::findMenu(core::string_view name)
    {
        core::ScopedLock<MenuContainer::ThreadPolicy> lock(menus_.getThreadPolicy());

        auto* pMenu = menus_.findAsset(name);
        if (pMenu) {
            return pMenu;
        }

        X_WARNING("MenuManager", "Failed to find menu: \"%.*s\"", name.length(), name.data());
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

        auto* pMenuRes = menus_.findAsset(core::string_view(name));
        if (!pMenuRes) {
            X_LOG1("MenuManager", "Not reloading \"%s\" it's not currently used", name.c_str());
            return false;
        }

        X_LOG0("MenuManager", "Reloading: %s", name.c_str());

        pAssetLoader_->reload(pMenuRes, core::ReloadFlag::Beginframe);
        return true;
    }

    void XMenuManager::listGuis(core::string_view searchPattern) const
    {
        X_UNUSED(searchPattern);
        X_ASSERT_NOT_IMPLEMENTED();
    }

    void XMenuManager::Cmd_ListUis(core::IConsoleCmdArgs* pCmd)
    {
        core::string_view searchPattern;

        if (pCmd->GetArgCount() >= 2) {
            searchPattern = pCmd->GetArg(1);
        }

        listGuis(searchPattern);
    }

} // namespace gui

X_NAMESPACE_END