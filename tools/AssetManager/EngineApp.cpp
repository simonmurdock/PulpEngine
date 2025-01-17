#include "stdafx.h"
#include "EngineApp.h"

#include <Platform\Module.h>
#include <Platform\MessageBox.h>

using namespace core::string_view_literals;


EngineApp::EngineApp() :
	pICore_(nullptr),
	hSystemHandle_(core::Module::NULL_HANDLE)
{
}


EngineApp::~EngineApp()
{
	ShutDown();

	if (hSystemHandle_) {
		core::Module::UnLoad(hSystemHandle_);
	}
}


bool EngineApp::Init(HINSTANCE hInstance, const wchar_t* sInCmdLine)
{
	CoreInitParams params;
	params.hInstance = hInstance;
	params.pCmdLine = sInCmdLine;
	params.bSkipInput = true;
	params.bSkipSound = true;
#if X_DEBUG
	params.bVsLog = true;
#else
	params.bVsLog = false;
#endif
	params.bConsoleLog = false;
	params.bTesting = false;
	params.bCoreOnly = true;
	params.bEnableBasicConsole = false;
	params.bEnableJobSystem = true; 
//	params.pConsoleWnd = nullptr;
	params.bFileSysWorkingDir = true;


#ifdef X_LIB

	pICore_ = CreateCoreInterface(params);

#else
	// load the dll.
	hSystemHandle_ = core::Module::Load(CORE_DLL_NAME);

	if (!hSystemHandle_)
	{
		Error(CORE_DLL_NAME" Loading Failed"_sv);
		return false;
	}

	CreateCoreInfterFaceFunc::Pointer pfnCreateCoreInterface =
		reinterpret_cast<CreateCoreInfterFaceFunc::Pointer>(
			core::Module::GetProc(hSystemHandle_, CORE_DLL_INITFUNC));

	if (!pfnCreateCoreInterface)
	{
		Error(CORE_DLL_NAME" not valid"_sv);
		return false;
	}

	pICore_ = pfnCreateCoreInterface(params);

#endif // !X_LIB

	if (!pICore_)
	{
		Error("Engine Init Failed"_sv);
		return false;
	}

	pICore_->RegisterAssertHandler(this);

	LinkModule(pICore_, "AssetManager");

	// ConverterLib
	if (!pICore_->IntializeLoadedEngineModule(X_ENGINE_OUTPUT_PREFIX "ConverterLib", "Engine_ConverterLib")) {
		return false;
	}

	// AssetDB
	if (!pICore_->IntializeLoadedEngineModule(X_ENGINE_OUTPUT_PREFIX "AssetDB", "Engine_AssetDB")) {
		return false;
	}

	// MatLib
	if (!pICore_->IntializeLoadedConverterModule(X_ENGINE_OUTPUT_PREFIX "MaterialLib", "Engine_MaterialLib")) {
		return false;
	}

	if (!pICore_->IntializeLoadedConverterModule(X_ENGINE_OUTPUT_PREFIX "FxLib", "Engine_FxLib")) {
		return false;
	}

	if (!pICore_->IntializeLoadedConverterModule(X_ENGINE_OUTPUT_PREFIX "ImgLib", "Engine_ImgLib")) {
		return false;
	}

	return true;
}

bool EngineApp::ShutDown(void)
{
	if (pICore_) {
		pICore_->UnRegisterAssertHandler(this);
		pICore_->Release();
	}
	pICore_ = nullptr;
	return true;
}

void EngineApp::Error(core::string_view errorText)
{
    core::msgbox::show(errorText,
        X_ENGINE_NAME " Start Error"_sv,
        core::msgbox::Style::Error | core::msgbox::Style::Topmost | core::msgbox::Style::DefaultDesktop,
        core::msgbox::Buttons::OK);
}

void EngineApp::OnAssert(const core::SourceInfo& sourceInfo)
{
	X_UNUSED(sourceInfo);

}

void EngineApp::OnAssertVariable(const core::SourceInfo& sourceInfo)
{
	X_UNUSED(sourceInfo);

}
