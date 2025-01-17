#include "stdafx.h"
#include "MenuHandler.h"

#include "Menu.h"
#include "MenuManger.h"
#include "GuiContex.h"
#include "Drawing\PrimitiveContext.h"

#include <IFrameData.h>

X_NAMESPACE_BEGIN(engine)

namespace gui
{
    
    MenuHandler::MenuHandler(GuiContex& ctx, XMenuManager& man) :
        ctx_(ctx),
        man_(man),
        pActiveMenu_(nullptr),
        firstMenu_(false)
    {

    }

    bool MenuHandler::isActive(void) const
    {
        return !stack_.isEmpty();
    }

    void MenuHandler::update(MenuParams& params, core::FrameData& frame, IPrimitiveContext* pIPrim)
    {
        auto* pPrim = static_cast<PrimitiveContext*>(pIPrim);

        if (stack_.isEmpty()) {
            return;
        }

        GuiContex::Params p;
        p.pSession = params.pSession;
        p.rect.set(0.f, 0.f, static_cast<float>(frame.view.displayRes.x), static_cast<float>(frame.view.displayRes.y));
        p.cursorPos = frame.input.cusorPosClient;
        p.frameDelta = frame.timeInfo.deltas[core::Timer::UI];

        ctx_.setPrimContet(pPrim);
        ctx_.setFont(gEnv->pFontSys->getDefault());
        ctx_.processInput(frame.input);
        ctx_.beginFrame(p);

        man_.setActiveHandler(this);

        // if menu active.. blar.. blar..
        auto* pMenu = stack_.top();
        if(pMenu->isLoaded())
        {
            if (pActiveMenu_ != pMenu)
            {
                if(firstMenu_)
                { 
                    firstMenu_ = false;
                    pMenu->onOpen();
                }

                pActiveMenu_ = pMenu;
            }

            ctx_.begin("Base", GuiContex::WindowFlags());

            pMenu->draw(frame);

            ctx_.end();
        }

#if X_SUPER == 0
        man_.setActiveHandler(nullptr);
#endif // !X_SUPER

        ctx_.endFrame();
    }

    bool MenuHandler::openMenu(core::string_view name)
    {
        auto* pMenu = static_cast<Menu*>(gEngEnv.pMenuMan_->loadMenu(name));
        if (!pMenu) {
            return false;
        }

        if (stack_.isEmpty()) {
            firstMenu_ = true;
        }

        stack_.push(pMenu);
        return true;
    }

    void MenuHandler::close(void)
    {
        pActiveMenu_ = nullptr;
        stack_.clear();
    }

    bool MenuHandler::back(bool close)
    {
        if (stack_.isEmpty()) {
            return false;
        }

        if (!close && stack_.size() < 2) {
            return false;
        }
        pActiveMenu_ = nullptr;
        stack_.pop();
        return true;
    }


} // namespace gui

X_NAMESPACE_END
