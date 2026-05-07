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
class SelectionList;
class ViewState;
class ViewContextPrivate;

/**
 * @class ViewContext
 * @brief Shared operating context for viewport-related widgets.
 *
 * ViewContext exposes externally owned session services and models needed by
 * reusable view widgets. It does not own the stage lock, command stack,
 * selection list, or view state. The owning controller decides which objects
 * are connected, allowing widgets to be driven by a full Session, a test
 * harness, or another data source.
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
     * @brief Returns true if the context has a stage lock.
     */
    bool hasStageLock() const;

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
     * @brief Returns true if the context has a command stack.
     */
    bool hasCommandStack() const;

    /**
     * @brief Sets the selection model used by view widgets.
     *
     * The selection list is owned externally, typically by the active session.
     */
    void setSelectionList(SelectionList* selectionList);

    /**
     * @brief Returns the current selection model.
     */
    SelectionList* selectionList() const;

    /**
     * @brief Returns true if the context has a selection model.
     */
    bool hasSelectionList() const;

    /**
     * @brief Sets the view state used by view widgets.
     *
     * The view state is owned externally, typically by the active session.
     */
    void setViewState(ViewState* viewState);

    /**
     * @brief Returns the current view state.
     */
    ViewState* viewState() const;

    /**
     * @brief Returns true if the context has a view state.
     */
    bool hasViewState() const;

    /**
     * @brief Returns true if the context has the minimum services needed for view operation.
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
