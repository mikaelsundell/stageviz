// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "stageviz.h"
#include <QScopedPointer>
#include <QWidget>

namespace stageviz {

class TimelineViewPrivate;

/**
 * @class TimelineView
 * @brief Timeline controls for scrubbing and playing stage animation.
 *
 * Displays the stage time range and sampled frames, and lets users scrub
 * or play the current stage time.
 */
class TimelineView : public QWidget {
    Q_OBJECT
public:
    /**
     * @brief Constructs the timeline view.
     */
    explicit TimelineView(QWidget* parent = nullptr);

    /**
     * @brief Destroys the timeline view.
     */
    ~TimelineView() override;

Q_SIGNALS:
    /**
     * @brief Emitted when the current stage time changes.
     */
    void timeCodeChanged(double timeCode);

private:
    QScopedPointer<TimelineViewPrivate> p;
};

}  // namespace stageviz
