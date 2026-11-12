// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 - present Mikael Sundell

#include "renderresources.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <pxr/base/plug/plugin.h>
#include <pxr/base/plug/registry.h>

namespace stageviz::renderresources {

QString
path(const QString& filename)
{
    const QString applicationDir = QCoreApplication::applicationDirPath();
#ifdef Q_OS_MAC
    QDir contentsDir(applicationDir);
    if (contentsDir.cdUp()) {
        const QFileInfo resource(contentsDir.filePath(QStringLiteral("Resources/") + filename));
        if (resource.isFile())
            return resource.absoluteFilePath();
    }
#endif
    const QFileInfo resource(QDir(applicationDir).filePath(QStringLiteral("resources/") + filename));
    return resource.isFile() ? resource.absoluteFilePath() : QString();
}

QString
defaultDomeTexture()
{
    const auto plugin = pxr::PlugRegistry::GetInstance().GetPluginWithName("hdx");
    return plugin ? QString::fromStdString(pxr::PlugFindPluginResource(plugin, "textures/StinsonBeach.hdr"))
                  : QString();
}

}  // namespace stageviz::renderresources
