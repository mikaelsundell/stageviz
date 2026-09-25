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

    QList<SdfPath> topLevelPaths(const QList<SdfPath>& paths)
    {
        QList<SdfPath> result;
        result.reserve(paths.size());
        for (const SdfPath& path : paths) {
            bool isChild = false;
            for (const SdfPath& other : paths) {
                if (path != other && path.HasPrefix(other)) {
                    isChild = true;
                    break;
                }
            }
            if (!isChild)
                result.append(path);
        }
        return result;
    }

    QList<SdfPath> uniquePaths(const QList<SdfPath>& paths)
    {
        QList<SdfPath> unique;
        unique.reserve(paths.size());
        for (const SdfPath& path : paths) {
            if (!path.IsEmpty() && !unique.contains(path))
                unique.append(path);
        }
        return unique;
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

        QList<SdfPath> result;
        result.reserve(sorted.size());

        for (const SdfPath& path : sorted) {
            bool covered = false;
            for (const SdfPath& existing : result) {
                if (path == existing || path.HasPrefix(existing)) {
                    covered = true;
                    break;
                }
            }
            if (!covered)
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

        QList<SdfPath> result;
        result.reserve(paths.size());

        for (const SdfPath& inputPath : paths) {
            bool keep = true;
            for (const SdfPath& removedPath : removedPaths) {
                if (isAffectedPath(inputPath, removedPath)) {
                    keep = false;
                    break;
                }
            }
            if (keep)
                result.append(inputPath);
        }
        return result;
    }

    QList<SdfPath> remapAffectedPaths(const QList<SdfPath>& paths, const SdfPath& oldPath, const SdfPath& newPath)
    {
        QList<SdfPath> result;
        result.reserve(paths.size());

        for (const SdfPath& inputPath : paths) {
            if (isAffectedPath(inputPath, oldPath))
                result.append(newPath.AppendPath(inputPath.MakeRelativePath(oldPath)));
            else
                result.append(inputPath);
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
        for (const TfToken& token : tokens)
            order.erase(std::remove(order.begin(), order.end(), token), order.end());
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
