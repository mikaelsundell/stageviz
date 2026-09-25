// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "materialutils.h"
#include "stageviz.h"
#include <QList>
#include <QString>
#include <functional>

class QMenu;
class QObject;
class QWidget;

namespace stageviz {

/**
 * @class MaterialMenu
 * @brief Shared material-node discovery, filtering, compatibility, and menu construction.
 *
 * MaterialMenu is the common node-selection layer used by MaterialGraph and
 * MaterialTree. It keeps free node creation, input-specific connection menus,
 * search, family filtering, typed MaterialX variants, and connection
 * compatibility consistent across the material editor.
 */
class MaterialMenu {
public:
    /**
     * @brief Material node family exposed by a menu choice.
     */
    enum class Family { MaterialX, UsdPreview };

    /**
     * @brief Result of testing whether two shader ports may be connected.
     */
    enum class Compatibility { Compatible, Incompatible };

    /**
     * @brief Concrete shader-node choice presented to the user.
     *
     * A choice contains the common display metadata plus the family-specific
     * identifiers required to create or connect the selected node.
     */
    struct Choice {
        Family family = Family::MaterialX;
        QString group;
        QString nodeName;
        QString displayName;
        QString dataType;

        QString nodeDef;
        MaterialXNodeDefinition materialXDefinition;

        QString shaderId;
        TfToken outputName;
        SdfValueTypeName outputType;
    };

    /**
     * @brief Describes which node choices should be exposed.
     */
    struct Request {
        /**
         * @brief Required input type for connection menus.
         *
         * An empty type means free node creation and therefore does not filter by
         * output compatibility.
         */
        SdfValueTypeName targetType;

        /**
         * @brief Show MaterialX before USD Preview when both families are visible.
         */
        bool materialXFirst = true;

        /**
         * @brief Include the searchable MaterialX chooser.
         */
        bool includeSearch = true;

        /**
         * @brief Restrict the result to MaterialX nodes.
         */
        bool materialXOnly = false;

        /**
         * @brief Restrict the result to USD Preview nodes.
         */
        bool usdPreviewOnly = false;

        /**
         * @brief Insert family groups directly into the supplied menu.
         */
        bool flattenFamily = false;
    };

    /**
     * @brief Returns the node choices matching a request.
     *
     * When targetType is set, only nodes with compatible outputs are returned.
     */
    static QList<Choice> choices(const Request& request);

    /**
     * @brief Populates a menu from a material-node request.
     *
     * The callback receives the concrete typed choice selected by the user.
     */
    static void populate(QMenu* menu, const Request& request, QObject* receiver,
                         const std::function<void(const Choice&)>& callback);

    /**
     * @brief Shows the searchable MaterialX node chooser.
     *
     * The search dialog uses the same candidate model and compatibility filtering
     * as populate().
     */
    static void showMaterialXSearch(QWidget* parent, const Request& request,
                                    const std::function<void(const Choice&)>& callback);

    /**
     * @brief Tests shader-port type compatibility.
     *
     * Exact type matches are supported together with the explicitly allowed
     * Float2/TexCoord2f storage-equivalent connection.
     */
    static Compatibility compatibility(const SdfValueTypeName& outputType, const SdfValueTypeName& inputType,
                                       bool materialXToMaterialX = false);

    /**
     * @brief Tests compatibility between two USD shader properties.
     *
     * The property types are read from the stage and evaluated using the same
     * policy as compatibility().
     */
    static Compatibility connectionCompatibility(const UsdStageRefPtr& stage, const SdfPath& sourceOutputPath,
                                                 const SdfPath& targetInputPath);
};

}  // namespace stageviz
