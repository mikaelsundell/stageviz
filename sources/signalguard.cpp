// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "signalguard.h"
#include <QVector>

namespace stageviz {

class SignalGuardPrivate : public QObject {
public:
    SignalGuardPrivate() = default;

    struct Data {
        QVector<QObject*> objects;
        bool guarding = false;
    };

    Data d;
};

SignalGuard::SignalGuard()
    : p(new SignalGuardPrivate)
{}

SignalGuard::~SignalGuard() = default;

void
SignalGuard::attach(QObject* object)
{
    if (!object)
        return;

    if (!p->d.objects.contains(object))
        p->d.objects.append(object);
}

void
SignalGuard::beginGuard()
{
    if (p->d.guarding)
        return;

    p->d.guarding = true;

    for (QObject* object : std::as_const(p->d.objects)) {
        if (object)
            object->blockSignals(true);
    }
}

void
SignalGuard::endGuard()
{
    if (!p->d.guarding)
        return;

    for (QObject* object : std::as_const(p->d.objects)) {
        if (object)
            object->blockSignals(false);
    }

    p->d.guarding = false;
}

bool
SignalGuard::isGuarding() const
{
    return p->d.guarding;
}

SignalGuard::Scope::Scope(SignalGuard* guard)
    : m_guard(guard)
{
    if (m_guard)
        m_guard->beginGuard();
}

SignalGuard::Scope::~Scope()
{
    if (m_guard)
        m_guard->endGuard();
}

}  // namespace stageviz
