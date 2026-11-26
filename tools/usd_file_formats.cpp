// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 - present Mikael Sundell

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <pxr/base/plug/registry.h>
#include <pxr/base/tf/setenv.h>
#include <pxr/usd/sdf/fileFormat.h>
#include <vector>

int
main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    parser.setApplicationDescription("Discover readable native USD formats without starting Stageviz or Python.");
    parser.addHelpOption();
    QCommandLineOption plugins("plugin-dir", "Use an existing bundled USD plugin directory (repeatable).", "directory");
    parser.addOption(plugins);
    parser.process(app);

    std::vector<std::string> paths;
    for (const QString& path : parser.values(plugins))
        if (QDir(path).exists())
            paths.push_back(QDir(path).canonicalPath().toStdString());
    // Match packaged Stageviz's USD search-path policy before the first Sdf lookup.
    if (!paths.empty()) {
        pxr::TfSetenv("PXR_DISABLE_STANDARD_PLUG_SEARCH_PATH", "1");
        pxr::PlugRegistry::GetInstance().RegisterPlugins(paths);
    }

    QJsonArray extensions;
    for (const auto& extension : pxr::SdfFileFormat::FindAllFileFormatExtensions())
        if (pxr::SdfFileFormat::FormatSupportsReading(extension))
            extensions.append(QString::fromStdString(extension));
    if (extensions.isEmpty()) {
        QTextStream(stderr) << "USD reported no readable file formats\n";
        return 1;
    }
    QTextStream(stdout) << QJsonDocument(QJsonObject { { "extensions", extensions } }).toJson(QJsonDocument::Compact)
                        << Qt::endl;
    return 0;
}
