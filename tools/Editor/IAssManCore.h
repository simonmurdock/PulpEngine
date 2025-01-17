#pragma once

#include <QObject>
#include "Context.h"

X_NAMESPACE_BEGIN(editor)

class IEditor;

class MyStatusBar;

class ICoreListener : public QObject
{
	Q_OBJECT
public:
	ICoreListener(QObject* pParent = nullptr) : QObject(pParent) {}
	virtual ~ICoreListener() = default;

	virtual bool editorAboutToClose(IEditor* pEditor) { return true; }
	virtual bool coreAboutToClose(void) { return true; }
};


class Editor;

class ICore : public QObject
{
	Q_OBJECT

public:
	ICore(Editor* pMainWindow);
	~ICore();

public:
	static ICore* instance(void);

	static QString versionString(void);

	static QWidget* mainWindow(void);
	static QWidget* dialogParent(void);
	static MyStatusBar* statusBar(void);

	static void raiseWindow(QWidget* pWidget);

	static IContext* currentContextObject(void);
	static void addContextObject(IContext* pContext);
	static void removeContextObject(IContext* pContext);

	static void registerListner(ICoreListener* pListener);
	static bool callCoreCloseListners(QCloseEvent* pEvent);
	static const QList<ICoreListener*> getCoreListners(void);


	static QSettings* settings(QSettings::Scope scope = QSettings::UserScope);
	static void saveSettings(void);

signals:
	void coreAboutToClose(void);
	void optionsDialogRequested(void);
	void saveSettingsRequested(void);
	void contextAboutToChange(const QList<IContext *>& context);
	void contextChanged(const QList<IContext*>& context, const Context& additionalContexts);

protected:
	QList<ICoreListener*> listeners_;
};



X_NAMESPACE_END