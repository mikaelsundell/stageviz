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
 * and emits text as it arrives. It can also retain a bounded history
 * for display in a UI view.
 */
class Console : public QObject {
    Q_OBJECT
public:
    explicit Console(QObject* parent = nullptr);
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
     * @brief Returns the full buffered text.
     */
    QString text() const;

    /**
     * @brief Returns buffered lines.
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
