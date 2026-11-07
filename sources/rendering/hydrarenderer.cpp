// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 - present Mikael Sundell

#include "hydrarenderer.h"
#include <pxr/base/plug/plugin.h>
#include <pxr/base/plug/registry.h>
#include <pxr/base/tf/type.h>
#include <pxr/imaging/hd/rendererPluginRegistry.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace stageviz {

QList<HydraRenderer::Plugin>
HydraRenderer::availableRenderers()
{
    HfPluginDescVector descriptions;
    HdRendererPluginRegistry::GetInstance().GetPluginDescs(&descriptions);
    QList<Plugin> result;
    result.reserve(static_cast<qsizetype>(descriptions.size()));
    for (const HfPluginDesc& description : descriptions) {
        Plugin item;
        item.id = QString::fromStdString(description.id.GetString());
        item.displayName = QString::fromStdString(description.displayName);
        item.priority = description.priority;
        const PlugPluginPtr plugin = PlugRegistry::GetInstance().GetPluginForType(
            TfType::FindByName(description.id.GetString()));
        if (plugin) {
            item.libraryPath = QString::fromStdString(plugin->GetPath());
            item.loaded = plugin->IsLoaded();
        }
        result.append(item);
    }
    return result;
}

}  // namespace stageviz
