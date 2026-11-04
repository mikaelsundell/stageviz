// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "usdutils.h"
#include "qtutils.h"
#include <QDir>
#include <QFileInfo>
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <pxr/base/gf/vec3d.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/vt/array.h>
#include <pxr/base/vt/value.h>
#include <pxr/usd/sdf/copyUtils.h>
#include <pxr/usd/sdf/namespaceEdit.h>
#include <pxr/usd/sdf/primSpec.h>
#include <pxr/usd/sdf/variantSetSpec.h>
#include <pxr/usd/sdf/variantSpec.h>
#include <pxr/usd/usd/attribute.h>
#include <pxr/usd/usd/editContext.h>
#include <pxr/usd/usd/editTarget.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usd/variantSets.h>
#include <pxr/usd/usdGeom/bboxCache.h>
#include <pxr/usd/usdGeom/gprim.h>
#include <pxr/usd/usdGeom/imageable.h>
#include <pxr/usd/usdGeom/modelAPI.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usdGeom/xform.h>
#include <pxr/usd/usdGeom/xformCache.h>
#include <pxr/usd/usdGeom/xformOp.h>
#include <pxr/usd/usdGeom/xformable.h>
#include <pxr/usd/usdShade/material.h>
#include <pxr/usd/usdShade/materialBindingAPI.h>
#include <stack>

namespace stageviz {

namespace layer {
    SdfAttributeSpecHandle ensureAttributeSpec(const SdfLayerHandle& layer, const SdfPath& specPath,
                                              const SdfValueTypeName& type, SdfVariability variability, bool custom)
    {
        if (!layer || !layer->PermissionToEdit() || !specPath.IsPrimPropertyPath() || !type)
            return {};
        const auto existing = layer->GetAttributeAtPath(specPath);
        if (existing)
            return existing->GetTypeName() == type ? existing : SdfAttributeSpecHandle();
        // GetPrimPath strips a trailing variant selection. A spec property's
        // actual owner is its parent path, including that variant selection.
        const auto prim = SdfCreatePrimInLayer(layer, specPath.GetParentPath());
        return prim ? SdfAttributeSpec::New(prim, specPath.GetName(), type, variability, custom)
                    : SdfAttributeSpecHandle();
    }
    bool validatePrim(const UsdStageRefPtr& stage, const SdfLayerHandle& layer, const SdfPath& path, QString& error,
                      bool requireStrongest)
    {
        if (!stage) {
            error = "stage missing";
            return false;
        }
        if (!layer) {
            error = "layer missing";
            return false;
        }

        const SdfPath primPath = path.IsPropertyPath() ? path.GetPrimPath() : path;
        if (primPath.IsEmpty() || primPath == SdfPath::AbsoluteRootPath()) {
            error = "invalid prim path";
            return false;
        }

        const UsdPrim prim = stage->GetPrimAtPath(primPath);
        if (!prim || !prim.IsValid()) {
            error = QString("prim missing: %1").arg(qt::SdfPathToQString(primPath));
            return false;
        }

        // Instance proxies are composed, read-only views of shared prototype data.
        // Instance roots themselves remain editable.
        if (prim.IsInstanceProxy()) {
            error = QString("cannot edit instance proxy: %1").arg(qt::SdfPathToQString(primPath));
            return false;
        }

        if (!layer->GetPrimAtPath(primPath)) {
            error = QString("prim is not authored in layer: %1").arg(qt::SdfPathToQString(primPath));
            return false;
        }

        if (requireStrongest) {
            const SdfPrimSpecHandleVector stack = prim.GetPrimStack();
            if (stack.empty() || !stack.front() || stack.front()->GetLayer() != layer) {
                error = QString("prim strongest opinion is not in layer: %1").arg(qt::SdfPathToQString(primPath));
                return false;
            }
        }
        return true;
    }

    bool validateParent(const UsdStageRefPtr& stage, const SdfLayerHandle& layer, const SdfPath& parentPath,
                        QString& error, bool requireStrongest)
    {
        if (!stage) {
            error = "stage missing";
            return false;
        }
        if (!layer) {
            error = "layer missing";
            return false;
        }
        if (parentPath.IsEmpty() || !parentPath.IsAbsolutePath()) {
            error = "invalid parent path";
            return false;
        }
        if (parentPath == SdfPath::AbsoluteRootPath())
            return true;

        return validatePrim(stage, layer, parentPath, error, requireStrongest);
    }

}  // namespace layer

namespace identifier {

    std::string makeValidIdentifier(const std::string& input)
    {
        if (input.empty())
            return "Prim";

        return TfMakeValidIdentifier(input);
    }

    QString makeSafeIdentifier(const UsdStageRefPtr& stage, const SdfPath& parentPath, const QString& inputName)
    {
        const QString baseName = qt::StringToQString(makeValidIdentifier(qt::QStringToString(inputName)));
        if (!stage || !parentPath.IsAbsolutePath())
            return baseName;

        const bool parentIsRoot = parentPath == SdfPath::AbsoluteRootPath();
        const UsdPrim parentPrim = parentIsRoot ? UsdPrim() : stage->GetPrimAtPath(parentPath);

        if (!parentIsRoot && (!parentPrim || !parentPrim.IsValid()))
            return baseName;

        const SdfLayerHandle layer = stage->GetEditTarget().GetLayer();

        auto childExists = [&](const QString& name) {
            const SdfPath childPath = parentPath.AppendChild(TfToken(qt::QStringToString(name)));

            const UsdPrim childPrim = stage->GetPrimAtPath(childPath);
            if (childPrim && childPrim.IsValid())
                return true;

            if (layer && layer->GetPrimAtPath(childPath))
                return true;

            return false;
        };

        if (!childExists(baseName))
            return baseName;

        for (int i = 1;; ++i) {
            const QString uniqueName = QString("%1_%2").arg(baseName).arg(i);
            if (!childExists(uniqueName))
                return uniqueName;
        }
    }

    QString makeSafeIdentifier(const UsdStageRefPtr& stage, const SdfPath& parentPath, const QString& inputName,
                               const SdfPath& ignorePath)
    {
        const QString baseName = qt::StringToQString(makeValidIdentifier(qt::QStringToString(inputName)));
        if (!stage || !parentPath.IsAbsolutePath())
            return baseName;

        const bool parentIsRoot = parentPath == SdfPath::AbsoluteRootPath();
        const UsdPrim parentPrim = parentIsRoot ? UsdPrim() : stage->GetPrimAtPath(parentPath);

        if (!parentIsRoot && (!parentPrim || !parentPrim.IsValid()))
            return baseName;

        const SdfLayerHandle layer = stage->GetEditTarget().GetLayer();

        auto childExists = [&](const QString& name) {
            const SdfPath childPath = parentPath.AppendChild(TfToken(qt::QStringToString(name)));

            if (childPath == ignorePath)
                return false;

            const UsdPrim childPrim = stage->GetPrimAtPath(childPath);
            if (childPrim && childPrim.IsValid())
                return true;

            if (layer && layer->GetPrimAtPath(childPath))
                return true;

            return false;
        };

        if (!childExists(baseName))
            return baseName;

        for (int i = 1;; ++i) {
            const QString uniqueName = QString("%1_%2").arg(baseName).arg(i);
            if (!childExists(uniqueName))
                return uniqueName;
        }
    }

}  // namespace identifier

namespace path {

    PathIndex::PathIndex(const QList<SdfPath>& input)
    {
        paths.reserve(static_cast<size_t>(input.size()));
        for (const auto& path : input)
            insert(path);
    }

    bool PathIndex::insert(const SdfPath& path)
    {
        return !path.IsEmpty() && paths.insert(path).second;
    }

    bool PathIndex::contains(const SdfPath& path) const
    {
        return paths.find(path) != paths.end();
    }

    SdfPath PathIndex::nearestAncestor(const SdfPath& path) const
    {
        if (!path.IsAbsolutePath()) {
            const auto prefixes = path.GetPrefixes();
            for (auto it = prefixes.rbegin(); it != prefixes.rend(); ++it) {
                if (contains(*it))
                    return *it;
            }
            return path.HasPrefix(SdfPath::ReflexiveRelativePath()) && contains(SdfPath::ReflexiveRelativePath())
                       ? SdfPath::ReflexiveRelativePath() : SdfPath();
        }
        for (SdfPath current = path; !current.IsEmpty(); current = current.GetParentPath()) {
            if (contains(current))
                return current;
            if (current == SdfPath::AbsoluteRootPath() || current == SdfPath::ReflexiveRelativePath())
                break;
        }
        return {};
    }

    SdfPath PathIndex::outermostAncestor(const SdfPath& path) const
    {
        if (!path.IsAbsolutePath()) {
            if (path.HasPrefix(SdfPath::ReflexiveRelativePath()) && contains(SdfPath::ReflexiveRelativePath()))
                return SdfPath::ReflexiveRelativePath();
            for (const auto& prefix : path.GetPrefixes()) {
                if (contains(prefix))
                    return prefix;
            }
            return {};
        }
        SdfPath result;
        for (SdfPath current = path; !current.IsEmpty(); current = current.GetParentPath()) {
            if (contains(current))
                result = current;
            if (current == SdfPath::AbsoluteRootPath() || current == SdfPath::ReflexiveRelativePath())
                break;
        }
        return result;
    }

    bool PathIndex::covers(const SdfPath& path) const
    {
        return !nearestAncestor(path).IsEmpty();
    }

    PathRelations::PathRelations(const QList<SdfPath>& paths) : roots(paths)
    {
        for (const auto& path : paths) {
            for (const auto& prefix : path.GetPrefixes())
                prefixes.insert(prefix);
            if (path.IsAbsolutePath())
                prefixes.insert(SdfPath::AbsoluteRootPath());
            else if (!path.IsEmpty())
                prefixes.insert(SdfPath::ReflexiveRelativePath());
        }
    }

    bool PathRelations::intersects(const SdfPath& path) const
    {
        return !path.IsEmpty() && (prefixes.contains(path) || roots.covers(path));
    }

    PathCollector::PathCollector(QList<SdfPath>& output) : output(output), index(output) {}

    void PathCollector::append(const SdfPath& path)
    {
        if (index.insert(path))
            output.append(path);
    }

    QList<SdfPath> stableRootPaths(const QList<SdfPath>& paths)
    {
        const PathIndex roots(paths);
        QList<SdfPath> result;
        result.reserve(paths.size());
        PathCollector collector(result);
        for (const auto& path : paths) {
            if (!path.IsEmpty() && roots.outermostAncestor(path) == path)
                collector.append(path);
        }
        return result;
    }

    QList<SdfPath> topLevelPaths(const QList<SdfPath>& paths)
    {
        const PathIndex roots(paths);
        QList<SdfPath> result;
        result.reserve(paths.size());
        for (const auto& path : paths) {
            // This legacy helper intentionally retains duplicates and empties.
            if (path.IsEmpty() || roots.outermostAncestor(path) == path)
                result.append(path);
        }
        return result;
    }

    QList<SdfPath> uniquePaths(const QList<SdfPath>& paths)
    {
        QList<SdfPath> result;
        result.reserve(paths.size());
        PathCollector collector(result);
        for (const auto& path : paths)
            collector.append(path);
        return result;
    }

    QList<SdfPath> minimalRootPaths(const QList<SdfPath>& paths)
    {
        QList<SdfPath> sorted;
        sorted.reserve(paths.size());

        for (const SdfPath& path : paths) {
            if (!path.IsEmpty())
                sorted.append(path);
        }

        std::sort(sorted.begin(), sorted.end(), [](const SdfPath& a, const SdfPath& b) {
            const size_t aDepth = a.GetPathElementCount();
            const size_t bDepth = b.GetPathElementCount();

            if (aDepth != bDepth)
                return aDepth < bDepth;

            return a.GetString() < b.GetString();
        });

        PathIndex accepted;

        QList<SdfPath> result;
        result.reserve(sorted.size());

        for (const SdfPath& path : sorted) {
            if (accepted.covers(path))
                continue;

            accepted.insert(path);
            result.append(path);
        }

        return result;
    }

    bool isAffectedPath(const SdfPath& selectedPath, const SdfPath& rootPath)
    {
        if (selectedPath.IsEmpty() || rootPath.IsEmpty())
            return false;

        const SdfPath selectedPrimPath = selectedPath.IsPropertyPath() ? selectedPath.GetPrimPath() : selectedPath;

        const SdfPath rootPrimPath = rootPath.IsPropertyPath() ? rootPath.GetPrimPath() : rootPath;

        return selectedPrimPath == rootPrimPath || selectedPrimPath.HasPrefix(rootPrimPath);
    }

    bool isWithinRoots(const QList<SdfPath>& mask, const SdfPath& path)
    {
        if (mask.isEmpty())
            return true;

        for (const SdfPath& maskPath : mask) {
            if (path == maskPath || path.HasPrefix(maskPath))
                return true;
        }

        return false;
    }

    bool isCoveredByRoots(const QList<SdfPath>& selection, const SdfPath& path)
    {
        for (const SdfPath& selectedPath : selection) {
            if (path == selectedPath || path.HasPrefix(selectedPath))
                return true;
        }

        return false;
    }

    QList<SdfPath> removeAffectedPaths(const QList<SdfPath>& paths, const QList<SdfPath>& removedPaths)
    {
        if (paths.isEmpty() || removedPaths.isEmpty())
            return paths;

        PathIndex removedRoots;

        for (const SdfPath& removedPath : removedPaths) {
            if (removedPath.IsEmpty())
                continue;

            const SdfPath primPath = removedPath.IsPropertyPath() ? removedPath.GetPrimPath() : removedPath;

            removedRoots.insert(primPath);
        }

        QList<SdfPath> result;
        result.reserve(paths.size());

        for (const SdfPath& inputPath : paths) {
            const SdfPath primPath = inputPath.IsPropertyPath() ? inputPath.GetPrimPath() : inputPath;

            if (!removedRoots.covers(primPath))
                result.append(inputPath);
        }

        return result;
    }

    QList<SdfPath> remapAffectedPaths(const QList<SdfPath>& paths, const SdfPath& oldPath, const SdfPath& newPath)
    {
        QList<SdfPath> result;
        result.reserve(paths.size());

        for (const SdfPath& inputPath : paths) {
            if (isAffectedPath(inputPath, oldPath)) {
                result.append(newPath.AppendPath(inputPath.MakeRelativePath(oldPath)));
            }
            else {
                result.append(inputPath);
            }
        }

        return result;
    }

    void appendUnique(QList<SdfPath>& paths, const SdfPath& path)
    {
        if (!path.IsEmpty() && !paths.contains(path))
            paths.append(path);
    }

    TfTokenVector removeTokens(TfTokenVector order, const TfTokenVector& tokens)
    {
        const std::unordered_set<TfToken, TfToken::HashFunctor> removed(tokens.begin(), tokens.end());
        order.erase(std::remove_if(order.begin(), order.end(), [&](const TfToken& token) {
            return removed.find(token) != removed.end();
        }), order.end());

        return order;
    }

    TfTokenVector insertTokens(TfTokenVector order, const TfTokenVector& tokens, int index)
    {
        if (tokens.empty())
            return order;

        const int safeIndex = qBound(0, index, static_cast<int>(order.size()));

        order.insert(order.begin() + safeIndex, tokens.begin(), tokens.end());

        return order;
    }

}  // namespace path

namespace stage {
    bool canUseAsNativeInstanceRoot(const UsdPrim& prim)
    {
        return prim && prim.IsValid() && !prim.IsInstanceProxy()
               && !(prim.IsA<UsdGeomGprim>() && prim.GetChildren().empty());
    }

    std::string compositionAssetPath(const SdfLayerHandle& destinationLayer, const QString& filename)
    {
        const QString absoluteFilename = QFileInfo(filename).absoluteFilePath();
        if (!destinationLayer || destinationLayer->IsAnonymous())
            return qt::QStringToString(QDir::fromNativeSeparators(absoluteFilename));

        const QString destinationFilename = QString::fromStdString(destinationLayer->GetRealPath());
        if (destinationFilename.isEmpty())
            return qt::QStringToString(QDir::fromNativeSeparators(absoluteFilename));

        const QDir directory(QFileInfo(destinationFilename).absolutePath());
        return qt::QStringToString(QDir::fromNativeSeparators(directory.relativeFilePath(absoluteFilename)));
    }
}  // namespace stage

}  // namespace stageviz
