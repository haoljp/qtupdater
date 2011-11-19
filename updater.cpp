#include "updater.h"
#include "updaterexception.h"
#include "fetchers/smartfetcher.h"
#include "scripthelpers/scriptconsole.h"

#include <QScriptEngine>
#include <QCoreApplication>
#include <QTimer>
#include <QtDebug>

#if defined(USE_SCRIPT_DEBUGGER)
#include <QScriptEngineDebugger>
#include <QAction>
#endif

//===========================================================================//

static void addConsoleObjectToEngine(QScriptEngine &engine);
static void addQuitFunctionToEngine(QScriptEngine &engine);
static void quitApplication();

//===========================================================================//

class UpdaterPrivate
{
public:
    UpdaterPrivate(Updater *parent, const QUrl updateScript, const QString appInstallPath, const QString appUserPath)
        : q_ptr(parent)
        , updateScript(updateScript)
        , appInstallPath(appInstallPath)
        , appUserPath(appUserPath)
        , fetcher(updateScript)
        , engine()
#if defined(USE_SCRIPT_DEBUGGER)
        , debugger()
#endif

    {
        if(updateScript.isEmpty())
            throw UpdaterException("Update script is empty");
    }

    Q_DECLARE_PUBLIC(Updater)
    Updater * const q_ptr;

    const QUrl updateScript;
    const QString appInstallPath;
    const QString appUserPath;

    SmartFetcher fetcher;
    QScriptEngine engine;

#if defined(USE_SCRIPT_DEBUGGER)
    QScriptEngineDebugger debugger;
#endif
};

//===========================================================================//

Updater::Updater(const QUrl updateScript, const QString appInstallPath, const QString appUserPath, QObject *parent)
    : QObject(parent)
    , d_ptr(new UpdaterPrivate(this, updateScript, appInstallPath, appUserPath))
{
    Q_D(Updater);

    connect(&d->fetcher, SIGNAL(done(QByteArray)), this, SLOT(onScriptFetchDone(QByteArray)));
    connect(&d->fetcher, SIGNAL(error(QString)), this, SIGNAL(scriptFetchError(QString)));

    d->fetcher.fetch();
}

Updater::~Updater()
{
    delete d_ptr;
}

void Updater::onScriptFetchDone(QByteArray data)
{
    Q_D(Updater);

#if defined(USE_SCRIPT_DEBUGGER)
    d->debugger.attachTo(&d->engine);
//    d->debugger.action(QScriptEngineDebugger::InterruptAction)->trigger();
#endif

    // Check JS syntax
    const QScriptSyntaxCheckResult checkResult = QScriptEngine::checkSyntax(data);
    if(checkResult.state() == QScriptSyntaxCheckResult::Intermediate)
    {
        qCritical() << "Updater script contains incomplete js program\n";
        qCritical() << data;
    }
    else if(checkResult.state() == QScriptSyntaxCheckResult::Error)
    {
        qCritical() << "Updater script contains error at line" <<
                       checkResult.errorLineNumber() << "column" <<
                       checkResult.errorColumnNumber() << "description" << checkResult.errorMessage();
        qCritical() << data;
    }

    if(checkResult.state() != QScriptSyntaxCheckResult::Valid)
    {
        quitApplication();
        return;
    }

    // Prepare JS engine
    addConsoleObjectToEngine(d->engine);
    addQuitFunctionToEngine(d->engine);

    // Run Script
    d->engine.evaluate(data);

    // Quit app after evaluating script
    quitApplication();
}

//===========================================================================//

static QScriptValue quitApplication(QScriptContext *context, QScriptEngine *engine)
{
    Q_UNUSED(context);
    quitApplication();
    return engine->nullValue();
}

void addConsoleObjectToEngine(QScriptEngine &engine)
{
    ScriptConsole *console = new ScriptConsole(&engine);
    QScriptValue scriptConsole = engine.newQObject(console);
    engine.globalObject().setProperty("console", scriptConsole);
}

void addQuitFunctionToEngine(QScriptEngine &engine)
{
    QScriptValue scriptQuitFun = engine.newFunction(quitApplication);
    engine.globalObject().setProperty("quit", scriptQuitFun);
    engine.globalObject().setProperty("abort", scriptQuitFun);
}

void quitApplication()
{
    QTimer::singleShot(0, QCoreApplication::instance(), SLOT(quit()));
}
