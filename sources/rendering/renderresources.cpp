// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 - present Mikael Sundell

#include "renderresources.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

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
    return path(QStringLiteral("scandinavia_1024x512.exr"));
}

QString
neutralDomeTexture()
{
    return path(QStringLiteral("scandinavia_bw_1024x512.exr"));
}

}  // namespace stageviz::renderresources
