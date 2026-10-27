// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "stageviz.h"
#include "treeitem.h"
#include <QCoreApplication>
#include <QList>
#include <QScopedPointer>
#include <QString>
#include <pxr/usd/sdf/primSpec.h>
#include <pxr/usd/usd/stage.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace stageviz {

class LayerItemPrivate;

/**
 * @class LayerItem
 * @brief Tree item representing a USD layer in the layer hierarchy.
 *
 * Stores the layer identifiers and presents the layer name, mute state,
 * edit permission, and authored opinions in the layer tree.
 */
class LayerItem : public TreeItem {
    Q_DECLARE_TR_FUNCTIONS(LayerItem)

public:
    /**
     * @brief Columns displayed for a layer row.
     */
    enum Column { Name = 0, Visibility = 1, Permission = 2, Opinions = 3 };

    /**
     * @brief Display state used to update a layer row.
     */
    struct State {
        QString identifier;
        QString parentIdentifier;
        QString name;
        QString tooltip;
        int primSpecCount = 0;
        bool root = false;
        bool editTarget = false;
        bool muted = false;
        bool directlyMuted = false;
        bool inheritedMuted = false;
        bool canMute = false;
        bool editable = true;
        bool canReorder = false;
    };

    /**
     * @brief Constructs a top-level layer item.
     *
     * @param parent Owning tree widget.
     */
    explicit LayerItem(QTreeWidget* parent);

    /**
     * @brief Destroys the LayerItem instance.
     */
    ~LayerItem() override;

    /**
     * @brief Updates the layer data and its presentation in the tree.
     *
     * @param state Current display state for the layer.
     */
    void setState(const State& state);

    /**
     * @brief Returns the identifier of the represented layer.
     */
    QString identifier() const;

    /**
     * @brief Returns the identifier of the parent layer.
     */
    QString parentIdentifier() const;

    /**
     * @brief Returns whether the layer inherits muteness from an ancestor.
     */
    bool isInheritedMuted() const;

    /**
     * @brief Sets the identifier of the represented layer.
     *
     * Used when an anonymous root layer is saved and receives a file identifier.
     *
     * @param identifier New layer identifier.
     */
    void setIdentifier(const QString& identifier);

    /**
     * @brief Counts the prim specs authored beneath a prim spec.
     *
     * @param spec Root prim spec to count.
     */
    static int countPrimSpecs(const SdfPrimSpecHandle& spec);

    /**
     * @brief Appends authored prim paths that are composed on the stage.
     *
     * @param spec Root prim spec to inspect.
     * @param stage Stage used to check composed prim paths.
     * @param paths Output list for composed prim paths.
     */
    static void appendComposedPrimPaths(const SdfPrimSpecHandle& spec, const UsdStageRefPtr& stage,
                                        QList<SdfPath>& paths);

    /**
     * @brief Returns semantic state flags for the item.
     */
    ItemStates itemStates() const override;

private:
    Q_DISABLE_COPY_MOVE(LayerItem)
    QScopedPointer<LayerItemPrivate> p;
};

}  // namespace stageviz
