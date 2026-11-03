// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include <functional>
#include <utility>

namespace stageviz {

class Session;

/**
 * @class Command
 * @brief Encapsulates an executable operation with optional undo support.
 *
 * A Command represents a unit of work that modifies session or stage state.
 * It provides a redo function (execute) and an optional undo function.
 *
 * Commands are intended to be executed through CommandStack, which manages
 * execution and undo/redo history.
 *
 * All commands operate on a Session, which provides access to the USD stage,
 * selection model, masking, and notification system.
 */
class Command {
public:
    /**
     * @brief Function signature used by commands.
     *
     * The function receives the active Session and is responsible for
     * performing the operation. Implementations may perform work
     * asynchronously but must ensure thread-safe access to the session.
     */
    using Func = std::function<void(Session*)>;

    /**
     * @brief Constructs a command with redo and optional undo functions.
     *
     * @param redo Function executed when the command runs.
     * @param undo Function executed when the command is undone. If omitted,
     *             the command is treated as non-undoable.
     */
    Command(Func redo, Func undo = Func())
        : m_redo(std::move(redo))
        , m_undo(std::move(undo))
    {}

    /**
     * @brief Executes the command (redo).
     *
     * @param s Active session.
     */
    void execute(Session* s)
    {
        if (m_redo)
            m_redo(s);
    }

    /**
     * @brief Undoes the command.
     *
     * @param s Active session.
     */
    void undo(Session* s)
    {
        if (m_undo)
            m_undo(s);
    }

    /**
     * @brief Returns whether the command provides an undo operation.
     */
    bool isUndoable() const { return static_cast<bool>(m_undo); }

private:
    /**
     * @brief Redo operation.
     */
    Func m_redo;
    /**
     * @brief Undo operation.
     */
    Func m_undo;
};

}  // namespace stageviz
