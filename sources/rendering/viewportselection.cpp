// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "viewportselection.h"
#include "application.h"
#include "bboxutils.h"
#include "commandselection.h"
#include "qtutils.h"
#include "renderengine.h"
#include "session.h"
#include "threadutils.h"
#include "tracelocks.h"
#include "ui.h"
#include "usdutils.h"
#include "viewcamera.h"
#include "viewcontext.h"
#include "viewporthost.h"
#include "viewportwidget.h"
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QPainter>
#include <QSet>
#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <limits>
#include <numeric>
#include <pxr/imaging/hdx/pickTask.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usdGeom/boundable.h>
#include <pxr/usd/usdGeom/xform.h>
#include <thread>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

namespace stageviz {

class ViewportSelectionPrivate {
public:
    SdfPath pickNearestPath(const QPoint& pos);
    void sweepEvent(const QRect& rect, QMouseEvent* event);
    bool isPathMaskedIn(const SdfPath& path) const;
    const QList<SdfPath>& cachedVisibleSelection();
    bool isSelectionVisible(const SdfPath& path) const;
    bool pickMaskedIntersection(const UsdImagingGLEngine::PickParams& pickParams, const GfFrustum& pickFrustum,
                                UsdImagingGLEngine::IntersectionResultVector* results);
    QPoint deviceRatio(QPoint value) const
    {
        return QPoint(qRound(value.x() * d.host->widget()->devicePixelRatioF()),
                      qRound(value.y() * d.host->widget()->devicePixelRatioF()));
    }
    class PrimBoundsBVH {
    public:
        struct Entry {
            SdfPath path;
            GfBBox3d bounds;
            GfRange3d aligned;
            bool instanceRoot = false;
            bool instanceProxy = false;
        };
        struct Node {
            GfRange3d bounds;
            size_t begin = 0;
            size_t end = 0;
            int left = -1;
            int right = -1;
        };

        class Intersector {
        public:
            explicit Intersector(const GfFrustum& frustum)
            {
                const std::vector<GfVec3d> nearCorners = frustum.ComputeCornersAtDistance(
                    frustum.GetNearFar().GetMin());
                const std::vector<GfVec3d> farCorners = frustum.ComputeCornersAtDistance(frustum.GetNearFar().GetMax());
                if (nearCorners.size() != 4 || farCorners.size() != 4)
                    return;

                for (size_t i = 0; i < 4; ++i) {
                    frustumCorners_[i] = nearCorners[i];
                    frustumCorners_[i + 4] = farCorners[i];
                }

                const std::array<std::array<size_t, 2>, 12> frustumEdgeIndices = { {
                    { { 0, 1 } },
                    { { 0, 2 } },
                    { { 1, 3 } },
                    { { 2, 3 } },
                    { { 4, 5 } },
                    { { 4, 6 } },
                    { { 5, 7 } },
                    { { 6, 7 } },
                    { { 0, 4 } },
                    { { 1, 5 } },
                    { { 2, 6 } },
                    { { 3, 7 } },
                } };

                axes_.reserve(6 + 3 + frustumEdgeIndices.size() * 3);
                const auto addAxis = [this](const GfVec3d& axis) {
                    if (axis.GetLength() > 1e-12)
                        axes_.push_back(axis);
                };

                // Face normals of the frustum.
                addAxis(GfCross(frustumCorners_[1] - frustumCorners_[0], frustumCorners_[2] - frustumCorners_[0]));
                addAxis(GfCross(frustumCorners_[5] - frustumCorners_[4], frustumCorners_[6] - frustumCorners_[4]));
                addAxis(GfCross(frustumCorners_[2] - frustumCorners_[0], frustumCorners_[4] - frustumCorners_[0]));
                addAxis(GfCross(frustumCorners_[3] - frustumCorners_[1], frustumCorners_[5] - frustumCorners_[1]));
                addAxis(GfCross(frustumCorners_[1] - frustumCorners_[0], frustumCorners_[4] - frustumCorners_[0]));
                addAxis(GfCross(frustumCorners_[3] - frustumCorners_[2], frustumCorners_[6] - frustumCorners_[2]));

                for (const auto& edge : frustumEdgeIndices)
                    frustumEdges_.push_back(frustumCorners_[edge[1]] - frustumCorners_[edge[0]]);
                valid_ = true;
            }

            bool intersects(const GfBBox3d& box) const
            {
                if (!valid_ || box.GetRange().IsEmpty())
                    return false;

                const GfRange3d& range = box.GetRange();
                const GfVec3d& min = range.GetMin();
                const GfVec3d& max = range.GetMax();
                std::array<GfVec3d, 8> boxCorners;
                for (size_t i = 0; i < boxCorners.size(); ++i) {
                    const GfVec3d local((i & 1) ? max[0] : min[0], (i & 2) ? max[1] : min[1],
                                        (i & 4) ? max[2] : min[2]);
                    boxCorners[i] = box.GetMatrix().Transform(local);
                }

                const GfMatrix4d& boxTransform = box.GetMatrix();
                const std::array<GfVec3d, 3> boxEdges = {
                    boxTransform.TransformDir(GfVec3d(1.0, 0.0, 0.0)),
                    boxTransform.TransformDir(GfVec3d(0.0, 1.0, 0.0)),
                    boxTransform.TransformDir(GfVec3d(0.0, 0.0, 1.0)),
                };
                const auto overlapsOnAxis = [&](const GfVec3d& axis) {
                    if (axis.GetLength() <= 1e-12)
                        return true;

                    double frustumMin = std::numeric_limits<double>::infinity();
                    double frustumMax = -std::numeric_limits<double>::infinity();
                    double boxMin = std::numeric_limits<double>::infinity();
                    double boxMax = -std::numeric_limits<double>::infinity();
                    for (const GfVec3d& corner : frustumCorners_) {
                        const double projection = GfDot(corner, axis);
                        frustumMin = std::min(frustumMin, projection);
                        frustumMax = std::max(frustumMax, projection);
                    }
                    for (const GfVec3d& corner : boxCorners) {
                        const double projection = GfDot(corner, axis);
                        boxMin = std::min(boxMin, projection);
                        boxMax = std::max(boxMax, projection);
                    }

                    const double tolerance = 1e-10
                                             * std::max({ 1.0, std::abs(frustumMin), std::abs(frustumMax),
                                                          std::abs(boxMin), std::abs(boxMax) });
                    return !(frustumMax < boxMin - tolerance || boxMax < frustumMin - tolerance);
                };
                for (const GfVec3d& axis : axes_)
                    if (!overlapsOnAxis(axis))
                        return false;
                if (!overlapsOnAxis(GfCross(boxEdges[1], boxEdges[2]))
                    || !overlapsOnAxis(GfCross(boxEdges[2], boxEdges[0]))
                    || !overlapsOnAxis(GfCross(boxEdges[0], boxEdges[1])))
                    return false;
                for (const GfVec3d& frustumEdge : frustumEdges_)
                    for (const GfVec3d& boxEdge : boxEdges)
                        if (!overlapsOnAxis(GfCross(frustumEdge, boxEdge)))
                            return false;
                return true;
            }

        private:
            std::array<GfVec3d, 8> frustumCorners_;
            std::vector<GfVec3d> frustumEdges_;
            std::vector<GfVec3d> axes_;
            bool valid_ = false;
        };

        void clear()
        {
            entries_.clear();
            nodes_.clear();
        }
        void build(const UsdStageRefPtr& stage)
        {
            clear();
            if (!stage)
                return;

            std::vector<UsdPrim> prims;
            for (const UsdPrim& prim : stage->Traverse(UsdTraverseInstanceProxies(UsdPrimAllPrimsPredicate))) {
                if (prim && prim.IsActive() && prim.IsDefined() && (UsdGeomBoundable(prim) || prim.IsInstance()))
                    prims.push_back(prim);
            }
            const size_t candidates = prims.size();
            const unsigned workers = threadutils::workerCount(candidates, 256);
            std::vector<unsigned char> visible(candidates, 0);
            threadutils::parallelForRanges(candidates, workers, [&](unsigned, size_t begin, size_t end) {
                for (size_t i = begin; i < end; ++i) {
                    const UsdGeomImageable imageable(prims[i]);
                    visible[i] = !(imageable && imageable.ComputeVisibility() == UsdGeomTokens->invisible);
                }
            });
            {
                // UsdGeomBBoxCache is mutable and shared-cache access is
                // serialized by bbox::CacheGuard. Give each worker a private
                // cache so independent prim bounds can be evaluated in
                // parallel while the caller holds the stage read lock.
                std::vector<std::unique_ptr<UsdGeomBBoxCache>> caches;
                caches.reserve(workers);
                for (unsigned worker = 0; worker < workers; ++worker) {
                    caches.push_back(std::make_unique<UsdGeomBBoxCache>(UsdTimeCode::Default(),
                                                                        UsdGeomImageable::GetOrderedPurposeTokens(),
                                                                        true));
                }
                std::vector<std::vector<Entry>> partialEntries(workers);
                threadutils::parallelForRanges(candidates, workers, [&](unsigned worker, size_t begin, size_t end) {
                    std::vector<Entry>& workerEntries = partialEntries[worker];
                    workerEntries.reserve(end - begin);
                    UsdGeomBBoxCache& cache = *caches[worker];
                    for (size_t i = begin; i < end; ++i) {
                        if (!visible[i])
                            continue;
                        GfBBox3d box = cache.ComputeWorldBound(prims[i]);
                        GfRange3d aligned = box.ComputeAlignedRange();
                        if (!aligned.IsEmpty())
                            workerEntries.push_back({ prims[i].GetPath(), box, aligned, prims[i].IsInstance(),
                                                      prims[i].IsInstanceProxy() });
                    }
                });
                for (std::vector<Entry>& workerEntries : partialEntries)
                    entries_.insert(entries_.end(), std::make_move_iterator(workerEntries.begin()),
                                    std::make_move_iterator(workerEntries.end()));
            }
            nodes_.reserve(entries_.size() * 2);
            if (!entries_.empty())
                buildNode(0, entries_.size());
        }
        std::vector<const Entry*> query(const GfFrustum& frustum) const
        {
            if (nodes_.empty() || !frustum.Intersects(GfBBox3d(nodes_[0].bounds))) {
                return {};
            }
            const Intersector intersector(frustum);
            const auto querySubtree = [this, &frustum, &intersector](int root) {
                std::vector<const Entry*> hits;
                std::vector<int> stack { root };
                while (!stack.empty()) {
                    const int id = stack.back();
                    stack.pop_back();
                    const Node& node = nodes_[id];
                    if (!frustum.Intersects(GfBBox3d(node.bounds)))
                        continue;
                    if (node.left >= 0) {
                        stack.push_back(node.left);
                        stack.push_back(node.right);
                    }
                    else {
                        for (size_t i = node.begin; i < node.end; ++i)
                            if (frustum.Intersects(entries_[i].bounds) && intersector.intersects(entries_[i].bounds))
                                hits.push_back(&entries_[i]);
                    }
                }
                return hits;
            };

            const unsigned workers = threadutils::workerCount(entries_.size(), 4096);
            if (entries_.size() < 4096 || workers < 2) {
                auto hits = querySubtree(0);
                return hits;
            }

            // Split the BVH into independent subtrees. Prune rejected nodes
            // before scheduling work, and keep one partition on this thread.
            std::vector<int> roots { 0 };
            while (roots.size() < workers) {
                size_t largest = roots.size();
                size_t largestCount = 0;
                for (size_t i = 0; i < roots.size(); ++i) {
                    const Node& node = nodes_[roots[i]];
                    const size_t count = node.end - node.begin;
                    if (node.left >= 0 && count > largestCount) {
                        largest = i;
                        largestCount = count;
                    }
                }
                if (largest == roots.size())
                    break;
                const Node& node = nodes_[roots[largest]];
                const int left = node.left;
                const int right = node.right;
                roots[largest] = left;
                roots.push_back(right);
            }

            // Use the same worker policy and execution helper as sweep visibility
            // and PropertyTree. Each worker owns its output; BVH data is immutable.
            std::vector<std::vector<const Entry*>> partial(roots.size());
            threadutils::parallelForRanges(roots.size(), static_cast<unsigned>(roots.size()),
                                           [&](unsigned, size_t begin, size_t end) {
                                               for (size_t i = begin; i < end; ++i)
                                                   partial[i] = querySubtree(roots[i]);
                                           });
            std::vector<const Entry*> result;
            size_t total = 0;
            for (const auto& hits : partial)
                total += hits.size();
            result.reserve(total);
            for (auto& hits : partial)
                result.insert(result.end(), std::make_move_iterator(hits.begin()), std::make_move_iterator(hits.end()));
            return result;
        }


    private:
        int buildNode(size_t begin, size_t end)
        {
            const int id = static_cast<int>(nodes_.size());
            nodes_.emplace_back();
            GfRange3d bounds;
            for (size_t i = begin; i < end; ++i)
                bounds.UnionWith(entries_[i].aligned);
            nodes_[id].bounds = bounds;
            nodes_[id].begin = begin;
            nodes_[id].end = end;
            if (end - begin <= 8)
                return id;
            const GfVec3d extent = bounds.GetSize();
            int axis = 0;
            if (extent[1] > extent[axis])
                axis = 1;
            if (extent[2] > extent[axis])
                axis = 2;
            const size_t middle = begin + (end - begin) / 2;
            std::nth_element(entries_.begin() + begin, entries_.begin() + middle, entries_.begin() + end,
                             [axis](const Entry& a, const Entry& b) {
                                 return (a.aligned.GetMin()[axis] + a.aligned.GetMax()[axis])
                                        < (b.aligned.GetMin()[axis] + b.aligned.GetMax()[axis]);
                             });
            const int left = buildNode(begin, middle);
            const int right = buildNode(middle, end);
            nodes_[id].left = left;
            nodes_[id].right = right;
            return id;
        }
        std::vector<Entry> entries_;
        std::vector<Node> nodes_;
    };
    struct Data {
        ViewportHost* host = nullptr;
        bool active = false;
        QPoint start;
        QPoint end;
        QPoint lastPickPosition;
        QList<SdfPath> lastPickPaths;
        int lastPickIndex = -1;
        PrimBoundsBVH sweepIndex;
        bool sweepIndexDirty = true;
        QList<SdfPath> visibleSelectionCache;
        bool visibleSelectionDirty = true;
    };
    Data d;
};

SdfPath
ViewportSelectionPrivate::pickNearestPath(const QPoint& pos)
{
    const auto light = d.host->widget()->pickLight(pos);
    if (!light.IsEmpty())
        return light;
    d.host->widget()->makeCurrent();
    if (!d.host->stage() || !d.host->renderEngine())
        return {};
#ifdef WIN32
    d.host->widget()->QOpenGLWidget::context()->functions()->glDepthMask(GL_TRUE);
#endif
    const QPoint devicePos = deviceRatio(pos);
    const GfVec4d viewport = d.host->widgetViewport();
    GfVec2d center((devicePos.x() - viewport[0]) / viewport[2], (devicePos.y() - viewport[1]) / viewport[3]);
    center[0] = center[0] * 2.0 - 1.0;
    center[1] = -1.0 * (center[1] * 2.0 - 1.0);
    const GfVec2d size(1.0 / viewport[2], 1.0 / viewport[3]);
    const GfCamera camera = d.host->viewCamera()->camera();
    const GfFrustum pickFrustum = camera.GetFrustum().ComputeNarrowedFrustum(center, size);
    const GfMatrix4d viewMatrix = pickFrustum.ComputeViewMatrix();
    const GfMatrix4d projectionMatrix = pickFrustum.ComputeProjectionMatrix();
    const GfVec3d cameraPosition = camera.GetTransform().ExtractTranslation();
    SdfPath nearestPath;
    double nearestDistance = std::numeric_limits<double>::max();
    READ_LOCKER(locker, d.host->context()->stageLock(), "stageLock");
    if (!d.host->stage())
        return {};

    auto testRoot = [&](const UsdPrim& root) {
        if (!root)
            return;

        const UsdImagingGLEngine::PickParams pickParams { HdxPickTokens->resolveNearestToCenter };
        UsdImagingGLEngine::IntersectionResultVector results;
        if (!d.host->renderEngine()->testIntersection(pickParams, viewMatrix, projectionMatrix, root, &results))
            return;

        for (const auto& result : results) {
            if (result.hitPrimPath.IsEmpty() || !d.host->pathMaskedIn(result.hitPrimPath))
                continue;
            const double distance = (result.hitPoint - cameraPosition).GetLength();
            if (distance < nearestDistance) {
                nearestDistance = distance;
                nearestPath = result.hitPrimPath;
            }
        }
    };
    if (d.host->maskPaths().isEmpty()) {
        testRoot(d.host->stage()->GetPseudoRoot());
    }
    else {
        for (const SdfPath& maskPath : d.host->maskPaths())
            testRoot(d.host->stage()->GetPrimAtPath(maskPath));
    }
    return nearestPath;
}

void
ViewportSelectionPrivate::sweepEvent(const QRect& rect, QMouseEvent* event)
{
    d.host->widget()->makeCurrent();
    if (!d.host->stage() || !d.host->renderEngine())
        return;

#ifdef WIN32
    d.host->widget()->QOpenGLWidget::context()->functions()->glDepthMask(GL_TRUE);
#endif

    constexpr int clickTolerance = 6;
    const QRect logicalRect = rect.normalized();
    const bool isClick = logicalRect.width() <= clickTolerance && logicalRect.height() <= clickTolerance;

    QList<SdfPath> selectedPaths;
    QList<SdfPath> selection = d.host->selectedPaths();

    const SdfPath lightHit = isClick ? d.host->widget()->pickLight(logicalRect.center()) : SdfPath();
    if (!lightHit.IsEmpty()) {
        selectedPaths.append(lightHit);
        d.lastPickPaths.clear();
        d.lastPickIndex = -1;
    }
    else if (isClick) {
        constexpr int pickCycleTolerance = 6;
        const QPoint clickPosition = logicalRect.center();

        const bool samePosition = d.lastPickIndex >= 0
                                  && std::abs(clickPosition.x() - d.lastPickPosition.x()) <= pickCycleTolerance
                                  && std::abs(clickPosition.y() - d.lastPickPosition.y()) <= pickCycleTolerance;

        if (!samePosition) {
            d.lastPickPaths.clear();
            d.lastPickIndex = -1;
        }

        const QPoint devicePos = deviceRatio(clickPosition);
        const GfVec2i framebufferSize = d.host->widgetSize();
        const GfVec2i pixel(devicePos.x(), framebufferSize[1] - 1 - devicePos.y());
        if (pixel[0] >= 0 && pixel[1] >= 0 && pixel[0] < framebufferSize[0] && pixel[1] < framebufferSize[1]) {
            SdfPath hitPath;
            {
                READ_LOCKER(locker, d.host->context()->stageLock(), "stageLock");

                if (d.host->stage())
                    hitPath = d.host->renderEngine()->pickNextIdAt(pixel, d.lastPickPaths);
            }

            if (hitPath.IsEmpty() && !d.lastPickPaths.isEmpty()) {
                d.lastPickPaths.clear();

                READ_LOCKER(locker, d.host->context()->stageLock(), "stageLock");

                if (d.host->stage())
                    hitPath = d.host->renderEngine()->pickNextIdAt(pixel, {});
            }

            if (!hitPath.IsEmpty() && d.host->pathMaskedIn(hitPath)) {
                d.lastPickPaths.append(hitPath);
                d.lastPickIndex = static_cast<int>(d.lastPickPaths.size()) - 1;
                d.lastPickPosition = clickPosition;

                selectedPaths.append(hitPath);
            }
            else {
                d.lastPickPaths.clear();
                d.lastPickIndex = -1;
            }
        }
    }
    else {
        const QPoint topLeft = deviceRatio(logicalRect.topLeft());
        const QPoint bottomRight = deviceRatio(logicalRect.bottomRight());
        const QRect deviceRect(topLeft, bottomRight);

        const GfVec4d viewport = d.host->widgetViewport();

        GfVec2d center(((deviceRect.left() + deviceRect.right()) * 0.5 - viewport[0]) / viewport[2],
                       ((deviceRect.top() + deviceRect.bottom()) * 0.5 - viewport[1]) / viewport[3]);

        center[0] = center[0] * 2.0 - 1.0;
        center[1] = -(center[1] * 2.0 - 1.0);

        const GfVec2d size(double(deviceRect.width()) / viewport[2], double(deviceRect.height()) / viewport[3]);
        const GfFrustum pickFrustum = d.host->viewCamera()->camera().GetFrustum().ComputeNarrowedFrustum(center, size);
        {
            READ_LOCKER(locker, d.host->context()->stageLock(), "stageLock");

            if (!d.host->stage())
                return;

            if (d.sweepIndexDirty) {
                d.sweepIndex.build(d.host->stage());

                d.sweepIndexDirty = false;
            }
            std::vector<const PrimBoundsBVH::Entry*> hits;
            hits = d.sweepIndex.query(pickFrustum);
            QSet<SdfPath> selectedInstanceRoots;
            QSet<SdfPath> selectedProxyPaths;
            selectedInstanceRoots.reserve(hits.size());
            selectedProxyPaths.reserve(hits.size());
            for (const PrimBoundsBVH::Entry* hit : hits) {
                const SdfPath& path = hit->path;
                if (d.host->pathMaskedIn(path))
                    selectedPaths.append(path);
                else
                    continue;
                if (hit->instanceRoot)
                    selectedInstanceRoots.insert(path);
                if (hit->instanceProxy)
                    selectedProxyPaths.insert(path);
            }

            // An instance can have both its own bound and proxy descendants
            // in the sweep. Classify hits once, then walk each proxy hit's
            // ancestors. This avoids scanning every hit for every instance.
            QSet<SdfPath> instanceRootsWithProxyHits;
            instanceRootsWithProxyHits.reserve(selectedInstanceRoots.size());
            for (const SdfPath& proxyPath : selectedProxyPaths) {
                for (SdfPath ancestor = proxyPath.GetParentPath(); !ancestor.IsEmpty();
                     ancestor = ancestor.GetParentPath()) {
                    if (selectedInstanceRoots.contains(ancestor))
                        instanceRootsWithProxyHits.insert(ancestor);
                }
            }
            selectedPaths.erase(std::remove_if(selectedPaths.begin(), selectedPaths.end(),
                                               [&](const SdfPath& path) {
                                                   return instanceRootsWithProxyHits.contains(path);
                                               }),
                                selectedPaths.end());
        }
        selectedPaths.append(d.host->widget()->lightPathsInRect(logicalRect));
        selectedPaths = path::uniquePaths(selectedPaths);
        d.lastPickPosition = QPoint();
        d.lastPickPaths.clear();
        d.lastPickIndex = -1;
    }

    bool update = false;
    if (!selectedPaths.isEmpty()) {
        if (event->modifiers() & Qt::ShiftModifier) {
            if (selectedPaths.size() > 16 || selection.size() > 16) {
                QSet<SdfPath> selectedSet(selection.begin(), selection.end());

                for (const SdfPath& path : selectedPaths) {
                    if (selectedSet.contains(path))
                        selectedSet.remove(path);
                    else
                        selectedSet.insert(path);
                }

                QList<SdfPath> updated;
                updated.reserve(selectedSet.size());

                for (const SdfPath& path : selection) {
                    if (selectedSet.remove(path))
                        updated.append(path);
                }

                for (const SdfPath& path : selectedPaths) {
                    if (selectedSet.remove(path))
                        updated.append(path);
                }

                selection = std::move(updated);
                update = true;
            }
            else {
                for (const SdfPath& path : selectedPaths) {
                    const qsizetype index = selection.indexOf(path);

                    if (index >= 0)
                        selection.removeAt(index);
                    else
                        selection.append(path);

                    update = true;
                }
            }
        }
        else if (selection != selectedPaths) {
            selection = selectedPaths;
            update = true;
        }
    }
    else if (!selection.isEmpty()) {
        selection.clear();
        d.visibleSelectionDirty = true;
        update = true;
    }
    if (update) {
        d.host->syncSelection(selection);
        d.host->context()->execute(new Command(selectPaths(selection)));
    }
    d.host->widget()->update();
}

bool
ViewportSelectionPrivate::isPathMaskedIn(const SdfPath& path) const
{
    if (path.IsEmpty())
        return false;

    if (d.host->maskPaths().isEmpty())
        return true;

    const SdfPath primPath = path.IsPropertyPath() ? path.GetPrimPath() : path;
    for (const SdfPath& maskedPath : d.host->maskPaths()) {
        const SdfPath maskedPrimPath = maskedPath.IsPropertyPath() ? maskedPath.GetPrimPath() : maskedPath;
        if (primPath == maskedPrimPath || primPath.HasPrefix(maskedPrimPath))
            return true;
    }
    return false;
}

const QList<SdfPath>&
ViewportSelectionPrivate::cachedVisibleSelection()
{
    if (d.visibleSelectionDirty) {
        d.visibleSelectionCache.clear();
        d.visibleSelectionCache.reserve(d.host->selectedPaths().size());
        // With no mask all selected paths are visible: avoid per-path checks.
        if (d.host->maskPaths().isEmpty()) {
            d.visibleSelectionCache = d.host->selectedPaths();
        }
        else {
            for (const SdfPath& path : d.host->selectedPaths()) {
                if (isSelectionVisible(path))
                    d.visibleSelectionCache.append(path);
            }
        }
        d.visibleSelectionDirty = false;
    }
    return d.visibleSelectionCache;
}

bool
ViewportSelectionPrivate::isSelectionVisible(const SdfPath& path) const
{
    if (path.IsEmpty())
        return false;

    if (d.host->maskPaths().isEmpty())
        return true;

    const SdfPath primPath = path.IsPropertyPath() ? path.GetPrimPath() : path;
    for (const SdfPath& maskedPath : d.host->maskPaths()) {
        const SdfPath maskedPrimPath = maskedPath.IsPropertyPath() ? maskedPath.GetPrimPath() : maskedPath;
        if (primPath == maskedPrimPath || primPath.HasPrefix(maskedPrimPath) || maskedPrimPath.HasPrefix(primPath))
            return true;
    }
    return false;
}

bool
ViewportSelectionPrivate::pickMaskedIntersection(const UsdImagingGLEngine::PickParams& pickParams,
                                                 const GfFrustum& pickFrustum,
                                                 UsdImagingGLEngine::IntersectionResultVector* results)
{
    if (!results)
        return false;

    results->clear();
    if (!d.host->stage() || !d.host->renderEngine())
        return false;
    const GfMatrix4d viewMatrix = pickFrustum.ComputeViewMatrix();
    const GfMatrix4d projectionMatrix = pickFrustum.ComputeProjectionMatrix();
    READ_LOCKER(locker, d.host->context()->stageLock(), "stageLock");
    if (!d.host->stage())
        return false;

    if (d.host->maskPaths().isEmpty()) {
        const bool hit = d.host->renderEngine()->testIntersection(pickParams, viewMatrix, projectionMatrix,
                                                                  d.host->stage()->GetPseudoRoot(), results);
        return hit;
    }

    bool hitAny = false;
    for (const SdfPath& maskPath : d.host->maskPaths()) {
        UsdPrim root = d.host->stage()->GetPrimAtPath(maskPath);
        if (!root) {
            continue;
        }

        UsdImagingGLEngine::IntersectionResultVector localResults;
        const bool hit = d.host->renderEngine()->testIntersection(pickParams, viewMatrix, projectionMatrix, root,
                                                                  &localResults);
        if (!hit)
            continue;

        for (const auto& item : localResults) {
            if (!item.hitPrimPath.IsEmpty() && d.host->pathMaskedIn(item.hitPrimPath))
                results->push_back(item);
        }
        if (!localResults.empty())
            hitAny = true;
    }
    return hitAny && !results->empty();
}

ViewportSelection::ViewportSelection(ViewportHost& host)
    : p(new ViewportSelectionPrivate())
{
    p->d.host = &host;
}
ViewportSelection::~ViewportSelection() = default;
void
ViewportSelection::resetPickCycle()
{
    p->d.lastPickPosition = QPoint();
    p->d.lastPickPaths.clear();
    p->d.lastPickIndex = -1;
}
void
ViewportSelection::reset()
{
    cancel();
    resetPickCycle();
    p->d.sweepIndex.clear();
    invalidateStage();
}
void
ViewportSelection::invalidateStage()
{
    p->d.sweepIndexDirty = true;
    invalidateVisibleSelection();
}
void
ViewportSelection::invalidateVisibleSelection()
{
    p->d.visibleSelectionDirty = true;
}
SdfPath
ViewportSelection::pickNearestPath(const QPoint& pos)
{
    return p->pickNearestPath(pos);
}
void
ViewportSelection::sweep(const QRect& rect, QMouseEvent* event)
{
    p->sweepEvent(rect, event);
}
bool
ViewportSelection::isPathMaskedIn(const SdfPath& path) const
{
    return p->isPathMaskedIn(path);
}
const QList<SdfPath>&
ViewportSelection::visiblePaths()
{
    return p->cachedVisibleSelection();
}

bool
ViewportSelection::active() const
{
    return p->d.active;
}
void
ViewportSelection::begin(const QPoint& pos)
{
    p->d.active = true;
    p->d.start = pos;
    p->d.end = pos;
    p->d.host->updateOverlay();
}
void
ViewportSelection::update(const QPoint& pos)
{
    p->d.end = pos;
    p->d.host->updateOverlay();
}
void
ViewportSelection::end(QMouseEvent* event)
{
    p->d.end = event->pos();
    p->sweepEvent(QRect(p->d.start, p->d.end), event);
    cancel();
}
void
ViewportSelection::cancel()
{
    p->d.active = false;
    p->d.host->updateOverlay();
}
void
ViewportSelection::draw(QPainter& painter)
{
    if (!p->d.active)
        return;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, false);
    const QColor sweep = ui()->color(UI::ColorRole::Sweep);
    QColor fill = sweep;
    fill.setAlphaF(sweep.alphaF() * (35.0 / 255.0));
    painter.setPen(QPen(sweep, 1));
    painter.setBrush(fill);
    painter.drawRect(QRect(p->d.start, p->d.end).normalized());
    painter.restore();
}

}  // namespace stageviz
