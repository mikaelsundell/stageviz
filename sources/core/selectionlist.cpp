// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "selectionlist.h"
#include "qtutils.h"
#include <QList>
#include <unordered_set>

namespace stageviz {
class SelectionListPrivate {
public:
    SelectionListPrivate();
    ~SelectionListPrivate();
    struct Data {
        QList<SdfPath> paths;
        std::unordered_set<SdfPath, SdfPath::Hash> membership;
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
    return p->d.membership.find(path) != p->d.membership.end();
}

void
SelectionList::addPaths(const QList<SdfPath>& paths)
{
    bool changed = false;
    for (const SdfPath& path : paths) {
        if (p->d.membership.insert(path).second) {
            p->d.paths.append(path);
            changed = true;
        }
    }
    if (changed) {
        Q_EMIT selectionChanged(p->d.paths);
    }
}

void
SelectionList::removePaths(const QList<SdfPath>& paths)
{
    if (paths.isEmpty() || p->d.paths.isEmpty())
        return;

    bool changed = false;
    for (const SdfPath& path : paths) {
        if (p->d.membership.erase(path))
            changed = true;
    }
    if (!changed)
        return;

    p->d.paths.removeIf([this](const SdfPath& path) { return p->d.membership.find(path) == p->d.membership.end(); });
    Q_EMIT selectionChanged(p->d.paths);
}

void
SelectionList::togglePaths(const QList<SdfPath>& paths)
{
    if (paths.isEmpty())
        return;

    // Process toggles in input order. A removed and re-added path moves to
    // the end, matching the original QList implementation.
    std::unordered_set<SdfPath, SdfPath::Hash> touched;
    touched.reserve(static_cast<size_t>(paths.size()));
    QList<SdfPath> insertionEvents;
    for (const SdfPath& path : paths) {
        touched.insert(path);
        if (!p->d.membership.erase(path)) {
            p->d.membership.insert(path);
            insertionEvents.append(path);
        }
    }

    // Keep untouched paths in their original order, then append paths in
    // the order of their last insertion event (if still selected).
    p->d.paths.removeIf([&](const SdfPath& path) { return touched.find(path) != touched.end(); });
    std::unordered_set<SdfPath, SdfPath::Hash> appended;
    QList<SdfPath> reversed;
    for (auto it = insertionEvents.crbegin(); it != insertionEvents.crend(); ++it) {
        if (p->d.membership.find(*it) != p->d.membership.end() && appended.insert(*it).second)
            reversed.append(*it);
    }
    for (auto it = reversed.crbegin(); it != reversed.crend(); ++it)
        p->d.paths.append(*it);

    Q_EMIT selectionChanged(p->d.paths);
}

void
SelectionList::updatePaths(const QList<SdfPath>& paths)
{
    QList<SdfPath> uniquePaths;
    std::unordered_set<SdfPath, SdfPath::Hash> membership;
    membership.reserve(static_cast<size_t>(paths.size()));
    for (const SdfPath& path : paths) {
        if (membership.insert(path).second)
            uniquePaths.append(path);
    }
    if (p->d.paths == uniquePaths)
        return;

    p->d.paths.swap(uniquePaths);
    p->d.membership.swap(membership);
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
    if (p->d.paths.isEmpty())
        return;
    p->d.paths.clear();
    p->d.membership.clear();
    Q_EMIT selectionChanged(p->d.paths);
}

SelectionList::~SelectionList() = default;

bool
SelectionList::isEmpty() const
{
    return p->d.paths.isEmpty();
}

bool
SelectionList::isValid() const
{
    return true;
}
}  // namespace stageviz
