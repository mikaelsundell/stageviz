// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once
#include <QProcess>
#include <QWidget>

namespace stageviz {
namespace os {

    /** @name Application Appearance */
    ///@{

    /**
 * @brief Applies a dark theme to the application.
 *
 * Typically adjusts platform-level UI settings or palette
 * configuration to use a dark appearance.
 */
    void setDarkTheme();

    ///@}

    /** @name Application Paths */
    ///@{

    /**
 * @brief Returns the absolute path to the running application.
 */
    QString getApplicationPath();

    /**
 * @brief Restores access to a previously persisted scoped path.
 *
 * Used on platforms that require security-scoped bookmarks
 * (such as macOS sandbox environments).
 *
 * @param bookmark Serialized bookmark data.
 *
 * @return Resolved filesystem path.
 */
    QString restoreScopedPath(const QString& bookmark);

    /**
 * @brief Persists a scoped filesystem path.
 *
 * Converts a path into a platform-specific bookmark or
 * persistent reference that can be restored later.
 *
 * @param bookmark Filesystem path or bookmark data.
 *
 * @return Serialized bookmark representation.
 */
    QString persistScopedPath(const QString& bookmark);

    ///@}

    /** @name Platform Utilities */
    ///@{

    /**
 * @brief Writes a message to the platform console.
 *
 * Useful for debugging or forwarding messages to
 * system logging facilities.
 *
 * @param message Message to output.
 */
    void console(const QString& message);

    ///@}

}  // namespace os
}  // namespace stageviz
