// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include <QTreeWidget>
#include <pxr/usd/usd/stage.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace stageviz {

class ProgressViewPrivate;

/**
 * @class ProgressView
 * @brief Displays progress and status information for the current USD scene.
 *
 * Provides a widget used to visualize processing progress or scene-related
 * statistics driven by the Session and SelectionList. The view updates
 * as the scene or selection changes.
 */
class ProgressView : public QWidget {
    Q_OBJECT
public:
    /**
     * @brief Constructs the progress view widget.
     *
     * @param parent Optional parent widget.
     */
    ProgressView(QWidget* parent = nullptr);

    /**
     * @brief Destroys the ProgressView instance.
     */
    virtual ~ProgressView();

private:
    QScopedPointer<ProgressViewPrivate> p;
};

}  // namespace stageviz
