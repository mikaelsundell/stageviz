// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "stageviz.h"

#include <QObject>
#include <QReadWriteLock>
#include <QScopedPointer>

namespace stageviz {

class Command;
class CommandStack;
class ViewContextPrivate;

/**
 * @class ViewContext
 * @brief Shared facade for widgets that need stage locking and command execution.
 *
 * Widgets receive stage and selection updates from their owning view/controller.
 * The context provides shared services that widgets do not own directly.
 */
class ViewContext : public QObject {
    Q_OBJECT
public:
    /**
     * @brief Constructs a view context.
     *
     * @param parent Optional QObject parent.
     */
    explicit ViewContext(QObject* parent = nullptr);

    /**
     * @brief Destroys the view context.
     */
    ~ViewContext() override;

    /**
     * @brief Sets the stage lock used for guarded stage access.
     *
     * The lock is owned externally, typically by the active session.
     */
    void setStageLock(QReadWriteLock* lock);

    /**
     * @brief Returns the current stage lock.
     */
    QReadWriteLock* stageLock() const;

    /**
     * @brief Sets the command stack used for command execution.
     *
     * The command stack is owned externally, typically by the active session.
     */
    void setCommandStack(CommandStack* commandStack);

    /**
     * @brief Returns the current command stack.
     */
    CommandStack* commandStack() const;

    /**
     * @brief Returns true if the context can provide stage locking.
     */
    bool hasStageLock() const;

    /**
     * @brief Returns true if the context can execute commands.
     */
    bool hasCommandStack() const;

    /**
     * @brief Returns true if the context is ready for widget use.
     */
    bool isValid() const;

    /**
     * @brief Runs a command through the configured command stack.
     */
    void run(Command* command) const;

private:
    Q_DISABLE_COPY_MOVE(ViewContext)
    QScopedPointer<ViewContextPrivate> p;
};

}  // namespace stageviz
