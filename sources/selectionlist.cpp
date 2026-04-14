// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "selectionlist.h"
#include "qtutils.h"
#include <QList>

namespace stageviz {
class SelectionListPrivate {
public:
    SelectionListPrivate();
    ~SelectionListPrivate();
    struct Data {
        QList<SdfPath> paths;
    };
    Data d;
};

SelectionListPrivate::SelectionListPrivate() {}

SelectionListPrivate::~SelectionListPrivate() {}

SelectionList::SelectionList(QObject* parent)
    : QObject(parent)
    , p(new SelectionListPrivate())
{}

bool
SelectionList::isSelected(const SdfPath& path) const
{
    return p->d.paths.contains(path);
}

void
SelectionList::addPaths(const QList<SdfPath>& paths)
{
    bool changed = false;
    for (const SdfPath& path : paths) {
        if (!p->d.paths.contains(path)) {
            p->d.paths.append(path);
            changed = true;
        }
    }
    if (changed)
        Q_EMIT selectionChanged(p->d.paths);
}

void
SelectionList::removePaths(const QList<SdfPath>& paths)
{
    bool changed = false;
    for (const SdfPath& path : paths) {
        if (p->d.paths.removeOne(path))
            changed = true;
    }
    if (changed)
        Q_EMIT selectionChanged(p->d.paths);
}

void
SelectionList::togglePaths(const QList<SdfPath>& paths)
{
    bool changed = false;
    for (const SdfPath& path : paths) {
        if (p->d.paths.contains(path)) {
            p->d.paths.removeOne(path);
            changed = true;
        }
        else {
            p->d.paths.append(path);
            changed = true;
        }
    }
    if (changed)
        Q_EMIT selectionChanged(p->d.paths);
}

void
SelectionList::updatePaths(const QList<SdfPath>& paths)
{
    if (p->d.paths == paths)
        return;
    p->d.paths = paths;
    Q_EMIT selectionChanged(p->d.paths);
}

QList<SdfPath>
SelectionList::paths() const
{
    return p->d.paths;
}

void
SelectionList::clear()
{
    if (p->d.paths.size()) {
        p->d.paths.clear();
    }
    Q_EMIT selectionChanged(p->d.paths);
}

SelectionList::~SelectionList() = default;

bool
SelectionList::isEmpty() const
{
    return p->d.paths.size();
}

bool
SelectionList::isValid() const
{
    return true;
}
}  // namespace stageviz
