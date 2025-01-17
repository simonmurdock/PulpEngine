#pragma once

#include <ICore.h>
#include <Platform\Module.h>

X_NAMESPACE_DECLARE(core,
                    class Console);

class EngineApp : public IAssertHandler
{
public:
    EngineApp();
    ~EngineApp() X_OVERRIDE;

    bool Init(HINSTANCE hInstance, const wchar_t* pInCmdLine);
    bool ShutDown(void);
    int MainLoop(void);

private:
    virtual void OnAssert(const core::SourceInfo& sourceInfo) X_OVERRIDE;
    virtual void OnAssertVariable(const core::SourceInfo& sourceInfo) X_OVERRIDE;

private:
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static void Error(core::string_view errorText);

private:
    bool PumpMessages();

private:
    core::Module::Handle hSystemHandle_;
    ICore* pICore_;
};
