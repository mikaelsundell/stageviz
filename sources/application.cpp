// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2022 - present Mikael Sundell.
// https://github.com/mikaelsundell/stageviz

#include "application.h"
#include "console.h"
#include "os.h"
#include "pythoninterpreter.h"
#include "qtutils.h"
#include "session.h"
#include "settings.h"
#include "style.h"
#include <QApplication>
#include <QDir>
#include <QPointer>
#include <pxr/base/plug/plugin.h>
#include <pxr/base/plug/registry.h>
#include <pxr/base/tf/setenv.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace stageviz {
class ApplicationPrivate {
public:
    ApplicationPrivate();
    ~ApplicationPrivate();
    void init();

public:
    struct Data {
        QScopedPointer<Console> console;
        QScopedPointer<PythonInterpreter> pythonInterpreter;
        QScopedPointer<Session> session;
        QScopedPointer<Style> style;
        QScopedPointer<Settings> settings;
    };
    Data d;
};

ApplicationPrivate::ApplicationPrivate() {}

ApplicationPrivate::~ApplicationPrivate() {}

void
ApplicationPrivate::init()
{
    d.console.reset(new Console());
    d.console->start();
    d.session.reset(new Session());
    d.settings.reset(new Settings());
    d.style.reset(new Style());
    d.pythonInterpreter.reset(new PythonInterpreter());  // after session
#ifdef NDEBUG
    QStringList pluginDirs;

    const QString pluginUsdDir = os::getApplicationPath() + "/plugin/usd";
    if (QDir(pluginUsdDir).exists())
        pluginDirs << pluginUsdDir;

    const QString usdDir = os::getApplicationPath() + "/usd";
    if (QDir(usdDir).exists())
        pluginDirs << usdDir;

    if (!pluginDirs.isEmpty()) {
        TfSetenv("PXR_DISABLE_STANDARD_PLUG_SEARCH_PATH", "1");

        std::vector<std::string> pluginPaths;
        pluginPaths.reserve(pluginDirs.size());
        for (const QString& dir : pluginDirs)
            pluginPaths.push_back(qt::QStringToString(dir));

        PlugRegistry& registry = PlugRegistry::GetInstance();
        registry.RegisterPlugins(pluginPaths);
    }
#else
    PlugRegistry& registry = PlugRegistry::GetInstance();
    const PlugPluginPtrVector plugins = registry.GetAllPlugins();
    qDebug() << "plugins";
    for (const auto& plugin : plugins)
        qDebug().noquote() << qt::StringToQString(plugin->GetPath());
#endif
}

Application::Application(int& argc, char** argv)
    : QApplication(argc, argv)
    , p(new ApplicationPrivate())
{
    p->init();
}

Application::~Application() {}

Console*
Application::console() const
{
    return p->d.console.data();
}

PythonInterpreter*
Application::pythonInterpreter() const
{
    return p->d.pythonInterpreter.data();
}

Session*
Application::session() const
{
    return p->d.session.data();
}

Settings*
Application::settings() const
{
    return p->d.settings.data();
}

Style*
Application::style() const
{
    return p->d.style.data();
}

Application*
Application::instance()
{
    auto* app = qobject_cast<Application*>(QCoreApplication::instance());
    if (!app) {
        qFatal("Fatal Error in [Application::instance()]:\n"
               "The global application instance is missing or is not a core::Application. "
               "Ensure you have instantiated stageviz::Application in your main() function.");
    }
    return app;
}
}  // namespace stageviz
