// Copyright 2022-present Contributors to the stageviz project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/mikaelsundell/stageviz

#include "os.h"
#include <QApplication>
#include <QDir>
#include <QScreen>
#include <windows.h>
#include <objbase.h>
#include <shlobj.h>

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

    bool revealFileInFolder(const QString& filePath)
    {
        const QString nativeFilePath = QDir::toNativeSeparators(filePath);
        const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (FAILED(comResult))
            return false;

        PIDLIST_ABSOLUTE itemIdList = nullptr;
        const HRESULT parseResult = SHParseDisplayName(reinterpret_cast<LPCWSTR>(nativeFilePath.utf16()), nullptr,
                                                       &itemIdList, 0, nullptr);
        const bool revealed = SUCCEEDED(parseResult)
                              && SUCCEEDED(SHOpenFolderAndSelectItems(itemIdList, 0, nullptr, 0));

        if (itemIdList)
            CoTaskMemFree(itemIdList);
        CoUninitialize();
        return revealed;
    }

    void console(const QString& message)
    {
        QString string = QStringLiteral("stageviz: %1\n").arg(message);
        OutputDebugStringW(reinterpret_cast<const wchar_t*>(string.utf16()));
    }

}  // namespace os
}  // namespace stageviz
