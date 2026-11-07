// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 - present Mikael Sundell

#pragma once

#include <QList>
#include <QString>

namespace stageviz {

/**
 * @brief Metadata-only entry point for independent Hydra rendering.
 *
 * This first implementation provides discovery only. Discovery does not load
 * renderer libraries, create delegates, or touch the viewport's render index.
 * A registered plugin is not necessarily compatible with the current process.
 */
class HydraRenderer final {
public:
    /**
     * @brief Describes a registered renderer without testing its availability.
     */
    struct Plugin {
        QString id;
        QString displayName;
        QString libraryPath;
        int priority = 0;
        bool loaded = false;
    };

    /**
     * @brief Returns registered plugins in Hydra registry priority order.
     *
     * Configure PXR_PLUGINPATH_NAME before starting Stageviz. This method only
     * reads plugin metadata; it does not call IsSupported or load libraries.
     * An empty result is valid when no renderer plugins are registered.
     *
     * @return Renderer identifiers, display names, and library metadata.
     */
    static QList<Plugin> availableRenderers();

private:
    HydraRenderer() = delete;
};

}  // namespace stageviz
