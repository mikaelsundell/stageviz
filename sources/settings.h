// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2024 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "stageviz.h"

#include <QObject>
#include <QScopedPointer>
#include <QVariant>

namespace stageviz {

class SettingsPrivate;

/**
 * @class Settings
 * @brief Application settings and configuration storage.
 *
 * Provides access to persistent viewer configuration such as
 * user preferences, UI state, and environment settings.
 * Settings are typically accessed through the global
 * Application instance.
 */
class Settings : public QObject {
public:
    /**
     * @brief Constructs the settings subsystem.
     *
     * @param parent Optional parent object.
     */
    Settings(QObject* parent = nullptr);

    /**
     * @brief Destroys the settings subsystem.
     */
    ~Settings() override;

    /**
     * @brief Returns the value for a key.
     */
    QVariant value(const QString& key, const QVariant& defaultValue = QVariant());

    /**
     * @brief Sets the value for a key.
     */
    void setValue(const QString& key, const QVariant& value);

private:
    Q_DISABLE_COPY_MOVE(Settings)
    QScopedPointer<SettingsPrivate> p;
};

}  // namespace stageviz
