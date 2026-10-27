// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "stageviz.h"
#include <QDialog>
#include <QScopedPointer>

class QShowEvent;

namespace stageviz {

class LayerDialogPrivate;

/**
 * @class LayerDialog
 * @brief Manages the USD layers used by the current Stageviz stage.
 *
 * LayerDialog provides layer creation and insertion, edit-target selection,
 * and layer operations through its LayerTree.
 */
class LayerDialog : public QDialog {
    Q_OBJECT
public:
    /** Creates the layer editor and connects it to the current session. */
    explicit LayerDialog(QWidget* parent = nullptr);

    /** Releases the layer editor. */
    ~LayerDialog() override;

    /** Refreshes the layer rows and their dirty state from the current stage. */
    void refreshLayers();

protected:
    void showEvent(QShowEvent* event) override;

Q_SIGNALS:
    /** Emitted after saving a single layer so the viewer can refresh its dirty state. */
    void layerSaved();

private:
    Q_DISABLE_COPY_MOVE(LayerDialog)
    QScopedPointer<LayerDialogPrivate> p;
};

}  // namespace stageviz
