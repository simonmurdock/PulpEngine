#include "stdafx.h"
#include "ICore.h"

#include "Editor.h"

X_NAMESPACE_BEGIN(editor)

namespace
{

	static ICore* pInstance = nullptr;
	static Editor* pMainwindow = nullptr;

} // namespace 


ICore::ICore(Editor* pMainWindow)
{
	pInstance = this;
	pMainwindow = pMainWindow;

}

ICore::~ICore()
{
	pInstance = nullptr;
	pMainwindow = nullptr;
}


ICore* ICore::instance(void)
{
	return pInstance;
}

QString ICore::versionString(void)
{
	return tr(X_ENGINE_NAME " - Editor %1").arg(QLatin1String(Constants::EDITOR_VERSION_LONG));
}

QWidget* ICore::mainWindow(void)
{
	return pMainwindow;
}

MyStatusBar* ICore::statusBar(void)
{
	return pMainwindow->statusBar();
}

QWidget* ICore::dialogParent(void)
{
	QWidget *active = QApplication::activeModalWidget();
	return active ? active : pMainwindow;
}


void ICore::raiseWindow(QWidget *widget)
{
	if (!widget) {
		return;
	}

	QWidget* pWindow = widget->window();
	if (pWindow == pMainwindow) {
		pMainwindow->raiseWindow();
	}
	else {
		pWindow->raise();
		pWindow->activateWindow();
	}
}


IContext* ICore::currentContextObject(void)
{
	return pMainwindow->currentContextObject();
}

void ICore::addContextObject(IContext* context)
{
	pMainwindow->addContextObject(context);
}

void ICore::removeContextObject(IContext* context)
{
	pMainwindow->removeContextObject(context);
}


void ICore::registerListner(ICoreListener* listener)
{
	pInstance->listeners_.push_back(listener);
}

bool ICore::callCoreCloseListners(QCloseEvent* event)
{
	for (ICoreListener* pListener : pInstance->listeners_) {
		if (!pListener->coreAboutToClose()) {
			event->ignore();
			return false;
		}
	}
	return true;
}

const QList<ICoreListener *> ICore::getCoreListners(void)
{
	return pInstance->listeners_;
}


QSettings* ICore::settings(QSettings::Scope scope)
{
	return pMainwindow->settings(scope);
}

void ICore::saveSettings(void)
{
	emit pInstance->saveSettingsRequested();

	ICore::settings(QSettings::SystemScope)->sync();
	ICore::settings(QSettings::UserScope)->sync();
}



X_NAMESPACE_END