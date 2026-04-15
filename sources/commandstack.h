// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "command.h"
#include "selectionlist.h"
#include "session.h"
#include "stageviz.h"

namespace stageviz {

class CommandStackPrivate;

/**
 * @class CommandStack
 * @brief Manages command execution with undo and redo support.
 *
 * Implements a command stack used to execute operations that modify
 * the scene or application state. Commands are stored in a history
 * allowing them to be undone or redone.
 *
 * The stack integrates with Session and SelectionModel so commands
 * can interact with the currently loaded USD stage and selection.
 */
class CommandStack : public QObject {
    Q_OBJECT
public:
    /**
     * @brief Constructs a command stack.
     *
     * @param parent Optional parent object.
     */
    CommandStack(QObject* parent = nullptr);

    /**
     * @brief Destroys the CommandStack instance.
     */
    virtual ~CommandStack();

    /** @name Command Execution */
    ///@{

    /**
     * @brief Run a command and adds it to the stack.
     *
     * @param command Command to execute.
     */
    void run(Command* command);

    /**
     * @brief Returns whether undo is available.
     */
    bool canUndo() const;

    /**
     * @brief Returns whether redo is available.
     */
    bool canRedo() const;

    /**
     * @brief Returns whether clear is available.
     */
    bool canClear() const;

    ///@}

public Q_SLOTS:

    /** @name History Navigation */
    ///@{

    /**
     * @brief Undoes the most recently executed command.
     */
    void undo();

    /**
     * @brief Redoes the most recently undone command.
     */
    void redo();

    /**
     * @brief Clears the command history.
     */
    void clear();

    ///@}

Q_SIGNALS:
    /**
     * @brief Emitted after a command is executed.
     */
    void commandExecuted(Command* command);

    /**
     * @brief Emitted when the command stack state changes.
     */
    void changed();

    /**
     * @brief Emitted when clear availability changes.
     */
    void canClearChanged(bool enabled);

    /**
     * @brief Emitted when undo availability changes.
     */
    void canUndoChanged(bool enabled);

    /**
     * @brief Emitted when redo availability changes.
     */
    void canRedoChanged(bool enabled);

private:
    QScopedPointer<CommandStackPrivate> p;
};

}  // namespace stageviz
