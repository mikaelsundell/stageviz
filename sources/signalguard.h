// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "stageviz.h"
#include <QObject>
#include <QScopedPointer>

namespace stageviz {

class SignalGuardPrivate;

/**
 * @class SignalGuard
 * @brief Utility for temporarily blocking signals on QObject instances.
 *
 * Provides a mechanism to guard sections of code where Qt signals
 * should be suppressed. This prevents recursive updates or unwanted
 * signal emissions while programmatically modifying widgets or models.
 *
 * Objects can be attached using attach(). When the guard is active,
 * signals on all attached objects are blocked.
 *
 * The guard can be controlled manually using beginGuard()/endGuard()
 * or automatically using the Scope helper for RAII-style guarding.
 */
class SignalGuard {
public:
    /**
     * @brief Constructs a SignalGuard.
     */
    SignalGuard();

    /**
     * @brief Destroys the SignalGuard.
     */
    ~SignalGuard();

    /** @name Guard Management */
    ///@{

    /**
     * @brief Attaches a QObject to the guard.
     *
     * Signals from this object will be blocked while the guard
     * is active.
     *
     * Multiple objects may be attached.
     *
     * @param object QObject to guard.
     */
    void attach(QObject* object);

    /**
     * @brief Begins the guarded section.
     *
     * Blocks signals on all attached objects.
     */
    void beginGuard();

    /**
     * @brief Ends the guarded section.
     *
     * Restores the original signal state of all attached objects.
     */
    void endGuard();

    /**
     * @brief Returns whether the guard is currently active.
     */
    bool isGuarding() const;

    ///@}

    /**
     * @class Scope
     * @brief RAII helper that automatically begins and ends a guard.
     *
     * Constructing a Scope begins guarding and destruction ends it.
     * This ensures signals are always restored even if the function
     * exits early.
     *
     * Example:
     * @code
     * SignalGuard::Scope guard(this);
     * @endcode
     */
    class Scope {
    public:
        /**
         * @brief Creates a scoped guard.
         * @param guard Guard instance to activate.
         */
        explicit Scope(SignalGuard* guard);

        /**
         * @brief Ends the guard when the scope exits.
         */
        ~Scope();

    private:
        SignalGuard* m_guard = nullptr;
    };

private:
    QScopedPointer<SignalGuardPrivate> p;
};

}  // namespace stageviz
