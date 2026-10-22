// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2022 - present Mikael Sundell.
// https://github.com/mikaelsundell/stageviz

#include "settings.h"
#include <QPointer>
#include <QSettings>

namespace stageviz {
class SettingsPrivate {
public:
    SettingsPrivate();
    ~SettingsPrivate();
    struct Data {};
    Data d;
};

SettingsPrivate::SettingsPrivate() {}

SettingsPrivate::~SettingsPrivate() {}

Settings::Settings(QObject* parent)
    : QObject(parent)
    , p(new SettingsPrivate)
{}


Settings::~Settings() {}

QVariant
Settings::value(const QString& key, const QVariant& defaultValue)
{
    QSettings settings(PROJECT_IDENTIFIER, PROJECT_NAME);
    return settings.value(key, defaultValue);
}

void
Settings::setValue(const QString& key, const QVariant& value)
{
    QSettings settings(PROJECT_IDENTIFIER, PROJECT_NAME);
    settings.setValue(key, value);
}

}  // namespace stageviz
