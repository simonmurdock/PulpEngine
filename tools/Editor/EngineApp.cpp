#include "stdafx.h"
#include "EngineApp.h"

#include <Platform\MessageBox.h>


EngineApp::EngineApp() :
	pICore_(nullptr),
	hSystemHandle_(nullptr)
{
}


EngineApp::~EngineApp()
{
	ShutDown();

	if (hSystemHandle_) {
		core::Module::UnLoad(hSystemHandle_);
	}
}


bool EngineApp::Init(const wchar_t* sInCmdLine, PLATFORM_HWND hWnd)
{
	SCoreInitParams params;
	params.hWnd = hWnd;
	params.hInstance = 0;
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
	params.bCoreOnly = false;
	params.bEnableBasicConsole = false;
	params.bEnableJobSystem = true; 
	params.pConsoleWnd = nullptr;
	params.pCoreArena = g_arena;
	params.bFileSysWorkingDir = false;


#ifdef X_LIB

	pICore_ = CreateCoreInterface(params);

#else
	// load the dll.
	hSystemHandle_ = core::Module::Load(CORE_DLL_NAME);

	if (!hSystemHandle_)
	{
		Error(CORE_DLL_NAME" Loading Failed");
		return false;
	}

	CreateCoreInfterFaceFunc::Pointer pfnCreateCoreInterface =
		reinterpret_cast<CreateCoreInfterFaceFunc::Pointer>(
			core::Module::GetProc(hSystemHandle_, CORE_DLL_INITFUNC));

	if (!pfnCreateCoreInterface)
	{
		Error(CORE_DLL_NAME" not valid");
		return false;
	}

	pICore_ = pfnCreateCoreInterface(params);

#endif // !X_LIB

	if (!pICore_)
	{
		Error("Engine Init Failed");
		return false;
	}

	pICore_->RegisterAssertHandler(this);

	LinkModule(pICore_, "Editor");

	// AssetDB
	if (!pICore_->IntializeLoadedEngineModule("Engine_AssetDB", "Engine_AssetDB")) {
		return false;
	}

	// MatLib
	if (!pICore_->IntializeLoadedConverterModule("Engine_MaterialLib", "Engine_MaterialLib")) {
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

void EngineApp::OnAssert(const core::SourceInfo& sourceInfo)
{
	X_UNUSED(sourceInfo);

}

void EngineApp::OnAssertVariable(const core::SourceInfo& sourceInfo)
{
	X_UNUSED(sourceInfo);

}


void EngineApp::Error(const char* pErrorText)
{
	core::msgbox::show(pErrorText,
		X_ENGINE_NAME" Start Error",
		core::msgbox::Style::Error | core::msgbox::Style::Topmost | core::msgbox::Style::DefaultDesktop,
		core::msgbox::Buttons::OK
	);
}

