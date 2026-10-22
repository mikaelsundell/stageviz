// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "stageviz.h"
#include <QObject>
#include <QScopedPointer>
#include <QStringList>

namespace stageviz {

class ConsolePrivate;

/**
 * @class Console
 * @brief Read-only console backend that captures stdout/stderr.
 *
 * The console redirects the current process standard output streams
 * and emits text as it arrives. It retains the most recent rows for display
 * when the UI view is opened.
 */
class Console : public QObject {
    Q_OBJECT
public:
    static constexpr int rowLimit = 100000;

    /**
     * @brief Creates the console backend; call start() to begin capture.
     */
    explicit Console(QObject* parent = nullptr);
    /**
     * @brief Stops capture and releases console resources.
     */
    ~Console() override;

    /**
     * @brief Starts capturing stdout/stderr.
     *
     * Returns true on success.
     */
    bool start();

    /**
     * @brief Stops capturing stdout/stderr and restores the originals.
     */
    void stop();

    /**
     * @brief Returns whether capture is active.
     */
    bool isRunning() const;

    /**
     * @brief Returns the retained console history, up to Console::rowLimit rows.
     */
    QString text() const;

    /**
     * @brief Returns the retained console history as lines.
     */
    QStringList lines() const;

Q_SIGNALS:
    /**
     * @brief Emitted when new text is captured.
     */
    void textAppended(const QString& text);

private:
    Q_DISABLE_COPY_MOVE(Console)
    QScopedPointer<ConsolePrivate> p;
};

}  // namespace stageviz
