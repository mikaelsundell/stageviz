
// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "application.h"
#include "pythoninterpreter.h"
#include "viewer.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QFuture>
#include <QTimer>
#include <QtConcurrent>

#include <pxr/usd/usd/schemaRegistry.h>

int
main(int argc, char* argv[])
{
    QFuture<void> schemas = QtConcurrent::run([]() { pxr::UsdSchemaRegistry::GetInstance(); });
    stageviz::Application app(argc, argv);
    schemas.waitForFinished();

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Stageviz USD viewer"));
    parser.addHelpOption();

    QCommandLineOption executeOption(QStringList { QStringLiteral("execute") },
                                     QStringLiteral("Execute Python code after the viewer starts."),
                                     QStringLiteral("code"));
    parser.addOption(executeOption);

    QCommandLineOption openOption(QStringList { QStringLiteral("open") },
                                  QStringLiteral("Open the specified USD file."), QStringLiteral("file"));
    parser.addOption(openOption);
    parser.addPositionalArgument(QStringLiteral("file"), QStringLiteral("USD file or Stageviz URL to open."),
                                 QStringLiteral("[file]"));
    parser.process(app);

    stageviz::Viewer viewer;
    QObject::connect(&app, &stageviz::Application::fileOpenRequested, &viewer, &stageviz::Viewer::openFile);
    QStringList viewerArguments { app.applicationFilePath() };
    if (parser.isSet(openOption)) {
        viewerArguments << QStringLiteral("--open") << parser.value(openOption);
    }
    else {
        viewerArguments.append(parser.positionalArguments());
    }
    viewer.setArguments(viewerArguments);
    viewer.show();

    if (parser.isSet(executeOption)) {
        const QString code = parser.value(executeOption);
        QTimer::singleShot(0, &viewer, [&app, code]() { app.pythonInterpreter()->executeScript(code); });
    }

    return app.exec();
}
