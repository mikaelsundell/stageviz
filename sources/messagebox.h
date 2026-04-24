// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include <QDialog>
#include <QUrl>

#ifdef Q_OS_WIN
#    ifdef MessageBox
#        undef MessageBox
#    endif
#endif

namespace stageviz {

class MessageBoxPrivate;

/**
 * @brief Application-styled modal message dialog.
 *
 * MessageBox provides convenience entry points for common dialog types while
 * keeping construction private. Use the static functions instead of creating
 * MessageBox instances directly.
 */
class MessageBox : public QDialog {
public:
    /**
     * @brief Shows an informational message.
     * @return true if the dialog was accepted, otherwise false.
     */
    static bool information(QWidget* parent, const QString& title, const QString& text);

    /**
     * @brief Shows a warning message.
     * @return true if the dialog was accepted, otherwise false.
     */
    static bool warning(QWidget* parent, const QString& title, const QString& text);

    /**
     * @brief Shows a question dialog.
     * @return true if the user accepted/confirmed, otherwise false.
     */
    static bool question(QWidget* parent, const QString& title, const QString& text);

    /**
     * @brief Shows an about dialog with heading, details, and an optional URL.
     * @return true if the dialog was accepted, otherwise false.
     */
    static bool about(QWidget* parent, const QString& title, const QString& heading, const QString& details,
                      const QString& url = QString());

    /**
     * @brief Shows an update dialog with heading, details, and an optional URL.
     * @return true if the dialog was accepted, otherwise false.
     */
    static bool update(QWidget* parent, const QString& title, const QString& heading, const QString& details,
                       const QString& url = QString());

private:
    MessageBox(QWidget* parent = nullptr);
    ~MessageBox();

private:
    QScopedPointer<MessageBoxPrivate> p;
};

}  // namespace stageviz