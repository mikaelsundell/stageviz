// Copyright 2022-present Contributors to the stageviz project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/mikaelsundell/stageviz

#include "os.h"
#include <QApplication>
#include <QScreen>
#include <windows.h>

namespace stageviz {
namespace os {
    void setDarkTheme() {}

    QString getApplicationPath() { return QApplication::applicationDirPath(); }

    QString restoreScopedPath(const QString& path)
    {
        return path;  // ignore on win32
    }

    QString persistScopedPath(const QString& path)
    {
        return path;  // ignore on win32
    }

    void console(const QString& message)
    {
        QString string = QStringLiteral("stageviz: %1\n").arg(message);
        OutputDebugStringW(reinterpret_cast<const wchar_t*>(string.utf16()));
    }

}  // namespace os
}  // namespace stageviz
