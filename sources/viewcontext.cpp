// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "viewcontext.h"
#include "command.h"
#include "commandstack.h"

namespace stageviz {

class ViewContextPrivate {
public:
    struct Data {
        QReadWriteLock* stageLock = nullptr;
        CommandStack* commandStack = nullptr;
    };
    Data d;
};

ViewContext::ViewContext(QObject* parent)
    : QObject(parent)
    , p(new ViewContextPrivate())
{}

ViewContext::~ViewContext() = default;

void
ViewContext::setStageLock(QReadWriteLock* lock)
{
    p->d.stageLock = lock;
}

QReadWriteLock*
ViewContext::stageLock() const
{
    return p->d.stageLock;
}

void
ViewContext::setCommandStack(CommandStack* commandStack)
{
    p->d.commandStack = commandStack;
}

CommandStack*
ViewContext::commandStack() const
{
    return p->d.commandStack;
}

bool
ViewContext::hasStageLock() const
{
    return p->d.stageLock != nullptr;
}

bool
ViewContext::hasCommandStack() const
{
    return p->d.commandStack != nullptr;
}

bool
ViewContext::isValid() const
{
    return hasStageLock() && hasCommandStack();
}

void
ViewContext::run(Command* command) const
{
    if (!command)
        return;
    if (!p->d.commandStack)
        return;

    p->d.commandStack->run(command);
}

}  // namespace stageviz
