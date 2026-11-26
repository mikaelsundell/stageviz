// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 - present Mikael Sundell

#include "commandmesh.h"
#include "commandutils.h"
#include "tracelocks.h"
#include "usdutils.h"
#include "viewstate.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <pxr/base/gf/rotation.h>
#include <pxr/imaging/geomUtil/capsuleMeshGenerator.h>
#include <pxr/imaging/geomUtil/coneMeshGenerator.h>
#include <pxr/imaging/geomUtil/cuboidMeshGenerator.h>
#include <pxr/imaging/geomUtil/cylinderMeshGenerator.h>
#include <pxr/imaging/geomUtil/planeMeshGenerator.h>
#include <pxr/imaging/geomUtil/sphereMeshGenerator.h>
#include <pxr/imaging/pxOsd/meshTopology.h>
#include <pxr/usd/sdf/changeBlock.h>
#include <pxr/usd/sdf/schema.h>
#include <pxr/usd/usd/editContext.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <set>
#include <vector>

namespace stageviz {
namespace {
    const TfTokenVector meshProperties {
        TfToken("points"),           TfToken("normals"),           TfToken("extent"),
        TfToken("faceVertexCounts"), TfToken("faceVertexIndices"), TfToken("subdivisionScheme")
    };

    bool supported(const UsdPrim& prim)
    {
        if (!prim || !prim.IsActive() || prim.IsInstance() || prim.IsInstanceProxy() || prim.IsPrototype()
            || prim.IsInPrototype())
            return false;
        const auto& type = prim.GetTypeName().GetString();
        return type == "Cube" || type == "Sphere" || type == "Cone" || type == "Cylinder" || type == "Capsule"
               || type == "Plane";
    }

    struct Sample {
        UsdTimeCode time;
        VtVec3fArray points;
        VtVec3fArray normals;
        VtVec3fArray extent;
    };

    struct MeshData {
        SdfPath path;
        PxOsdMeshTopology topology;
        TfToken interpolation;
        std::vector<Sample> samples;
    };

    bool generate(const UsdPrim& prim, int segments, MeshData& data)
    {
        const auto type = prim.GetTypeName().GetString();
        TfTokenVector dimensions;
        if (type == "Cube")
            dimensions = { TfToken("size") };
        else if (type == "Plane")
            dimensions = { TfToken("width"), TfToken("length"), TfToken("axis") };
        else {
            dimensions = { TfToken("radius") };
            if (type != "Sphere") {
                dimensions.push_back(TfToken("height"));
                dimensions.push_back(TfToken("axis"));
            }
        }
        std::set<double> times;
        for (const auto& name : dimensions) {
            std::vector<double> samples;
            prim.GetAttribute(name).GetTimeSamples(&samples);
            times.insert(samples.begin(), samples.end());
        }
        std::vector<UsdTimeCode> codes { UsdTimeCode::Default() };
        for (double time : times)
            codes.emplace_back(time);
        const size_t radial = segments, axial = segments / 2;
        data.path = prim.GetPath();
        for (const auto& time : codes) {
            const auto number = [&](const char* name, double& result) {
                const auto attr = prim.GetAttribute(TfToken(name));
                if (!attr.Get(&result, time) && !(time.IsDefault() && attr.Get(&result, UsdTimeCode::EarliestTime())))
                    return false;
                return std::isfinite(result) && result >= 0;
            };
            double radius = 1, height = 2, size = 2, width = 2, length = 2;
            if ((type == "Cube" && !number("size", size))
                || (type == "Plane" && (!number("width", width) || !number("length", length)))
                || (type != "Cube" && type != "Plane" && !number("radius", radius))
                || ((type == "Cone" || type == "Cylinder" || type == "Capsule") && !number("height", height)))
                return false;
            // Degenerate round surfaces do not have well-defined generated normals.
            if (radius <= 0 || (type == "Cone" && height <= 0))
                return false;
            TfToken axis("Z");
            const auto axisAttr = prim.GetAttribute(TfToken("axis"));
            if (axisAttr && !axisAttr.Get(&axis, time))
                axisAttr.Get(&axis, UsdTimeCode::EarliestTime());
            GfMatrix4d frame(1);
            if (axis == TfToken("X"))
                frame.SetRotate(GfRotation(GfVec3d(0, 0, 1), GfVec3d(1, 0, 0)));
            else if (axis == TfToken("Y"))
                frame.SetRotate(GfRotation(GfVec3d(0, 0, 1), GfVec3d(0, 1, 0)));
            else if (axis != TfToken("Z"))
                return false;
            Sample sample;
            sample.time = time;
            if (type == "Cube") {
                using G = GeomUtilCuboidMeshGenerator;
                data.topology = G::GenerateTopology();
                data.interpolation = G::GetNormalsInterpolation();
                sample.points.resize(G::ComputeNumPoints());
                sample.normals.resize(G::ComputeNumNormals());
                G::GeneratePoints(sample.points.begin(), size, size, size);
                G::GenerateNormals(sample.normals.begin());
            }
            else if (type == "Sphere") {
                using G = GeomUtilSphereMeshGenerator;
                data.topology = G::GenerateTopology(radial, axial);
                data.interpolation = G::GetNormalsInterpolation();
                sample.points.resize(G::ComputeNumPoints(radial, axial));
                sample.normals.resize(G::ComputeNumNormals(radial, axial));
                G::GeneratePoints(sample.points.begin(), radial, axial, radius);
                G::GenerateNormals(sample.normals.begin(), radial, axial);
            }
            else if (type == "Capsule") {
                using G = GeomUtilCapsuleMeshGenerator;
                data.topology = G::GenerateTopology(radial, axial);
                data.interpolation = G::GetNormalsInterpolation();
                sample.points.resize(G::ComputeNumPoints(radial, axial));
                sample.normals.resize(G::ComputeNumNormals(radial, axial));
                G::GeneratePoints(sample.points.begin(), radial, axial, radius, height, &frame);
                G::GenerateNormals(sample.normals.begin(), radial, axial, radius, height, &frame);
            }
            else if (type == "Cylinder") {
                using G = GeomUtilCylinderMeshGenerator;
                data.topology = G::GenerateTopology(radial);
                data.interpolation = G::GetNormalsInterpolation();
                sample.points.resize(G::ComputeNumPoints(radial));
                sample.normals.resize(G::ComputeNumNormals(radial));
                G::GeneratePoints(sample.points.begin(), radial, radius, height, &frame);
                G::GenerateNormals(sample.normals.begin(), radial, radius, height, &frame);
            }
            else if (type == "Cone") {
                using G = GeomUtilConeMeshGenerator;
                data.topology = G::GenerateTopology(radial);
                data.interpolation = G::GetNormalsInterpolation();
                sample.points.resize(G::ComputeNumPoints(radial));
                sample.normals.resize(G::ComputeNumNormals(radial));
                G::GeneratePoints(sample.points.begin(), radial, radius, height, &frame);
                G::GenerateNormals(sample.normals.begin(), radial, radius, height, &frame);
            }
            else {
                using G = GeomUtilPlaneMeshGenerator;
                data.topology = G::GenerateTopology();
                data.interpolation = G::GetNormalsInterpolation();
                sample.points.resize(G::ComputeNumPoints());
                sample.normals.resize(G::ComputeNumNormals());
                G::GeneratePoints(sample.points.begin(), width, length, &frame);
                G::GenerateNormals(sample.normals.begin(), &frame);
            }
            const auto finite = [](const GfVec3f& value) {
                return std::isfinite(value[0]) && std::isfinite(value[1]) && std::isfinite(value[2]);
            };
            if (!std::all_of(sample.points.begin(), sample.points.end(), finite)
                || !std::all_of(sample.normals.begin(), sample.normals.end(), finite)
                || !UsdGeomPointBased::ComputeExtent(sample.points, &sample.extent))
                return false;
            data.samples.push_back(std::move(sample));
        }
        return true;
    }

    struct Item {
        SdfPath path;
        SdfPath specPath;
        bool hadType = false;
        VtValue type;
        QList<SdfPath> createdSpecs;
        QList<command::DependencyPropertyState> properties;
    };

    bool restore(const SdfLayerHandle& layer, const QList<Item>& items, QString& error)
    {
        SdfChangeBlock block;
        for (auto it = items.crbegin(); it != items.crend(); ++it) {
            for (const auto& name : meshProperties) {
                const auto path = it->specPath.AppendProperty(name);
                if (layer->GetPropertyAtPath(path) && !command::removePropertySpec(layer, path))
                    return false;
            }
            for (const auto& property : it->properties)
                if (!command::restoreDependencyProperty(layer, property, error))
                    return false;
            if (it->hadType)
                layer->SetField(it->specPath, SdfFieldKeys->TypeName, it->type);
            else if (layer->GetPrimAtPath(it->specPath))
                layer->EraseField(it->specPath, SdfFieldKeys->TypeName);
            for (const auto& path : it->createdSpecs) {
                const auto spec = layer->GetPrimAtPath(path);
                if (spec && spec->IsInert())
                    stage::removePrimSpec(layer, path);
            }
        }
        return true;
    }
}  // namespace

bool
canConvertToMesh(const UsdStageRefPtr& stage, const QList<SdfPath>& paths)
{
    if (!stage || paths.isEmpty())
        return false;
    const auto target = stage->GetEditTarget();
    if (!target.GetLayer() || !target.GetLayer()->PermissionToEdit())
        return false;
    for (const auto& path : paths)
        if (!path.IsPrimPath() || !supported(stage->GetPrimAtPath(path)) || target.MapToSpecPath(path).IsEmpty())
            return false;
    return true;
}

Command
convertToMesh(const QList<SdfPath>& paths, int segments)
{
    struct State {
        UsdStageRefPtr stage;
        UsdEditTarget target;
        QList<Item> items;
        int segments = 0;
    };
    const auto state = std::make_shared<State>();
    const auto execute = [state, paths, segments](Session* session, bool undo) {
        if (!session || paths.isEmpty())
            return;
        if (!undo && state->segments == 0) {
            state->segments = segments;
            if (segments == 0) {
                const auto viewState = session->viewState();
                const auto level = viewState ? viewState->complexityLevel() : ViewState::Low;
                switch (level) {
                case ViewState::Low: state->segments = 16; break;
                case ViewState::Medium: state->segments = 32; break;
                case ViewState::High: state->segments = 64; break;
                case ViewState::VeryHigh: state->segments = 128; break;
                }
            }
        }
        command::beginDeferred(session, undo ? "Undo convert to mesh" : "Convert to mesh", 1);
        command::runWorker([state, paths, session, undo]() {
            const int segments = state->segments;
            QString error;
            QList<SdfPath> changed;
            {
                WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                const auto stage = session->stageUnsafe();
                if (!state->stage && stage) {
                    state->stage = stage;
                    state->target = stage->GetEditTarget();
                }
                const auto layer = state->target.GetLayer();
                if (!stage || stage != state->stage || !layer || !layer->PermissionToEdit())
                    error = "original stage or edit layer is unavailable or locked";
                else if (undo) {
                    if (restore(layer, state->items, error))
                        for (const auto& item : state->items)
                            changed.append(item.path);
                    else if (error.isEmpty())
                        error = "could not restore mesh opinions";
                }
                else {
                    UsdEditContext context(stage, state->target);
                    if (segments < 4 || segments > 256)
                        error = "segments must be between 4 and 256";
                    else if (!canConvertToMesh(stage, paths))
                        error = "select editable Cube, Sphere, Cone, Cylinder, Capsule or Plane prims";
                    else {
                        std::vector<MeshData> meshes;
                        for (const auto& path : path::uniquePaths(paths)) {
                            MeshData mesh;
                            if (!generate(stage->GetPrimAtPath(path), segments, mesh)) {
                                error = "invalid shape dimensions at " + command::pathText(path);
                                break;
                            }
                            meshes.push_back(std::move(mesh));
                        }
                        if (error.isEmpty()) {
                            state->items.clear();
                            for (const auto& mesh : meshes) {
                                Item item;
                                item.path = mesh.path;
                                item.specPath = state->target.MapToSpecPath(mesh.path);
                                item.hadType = layer->HasField(item.specPath, SdfFieldKeys->TypeName);
                                if (item.hadType)
                                    item.type = layer->GetField(item.specPath, SdfFieldKeys->TypeName);
                                for (auto path = item.specPath; path.IsPrimPath(); path = path.GetParentPath())
                                    if (!layer->GetPrimAtPath(path))
                                        item.createdSpecs.append(path);
                                for (const auto& name : meshProperties) {
                                    const auto path = item.specPath.AppendProperty(name);
                                    if (!layer->GetPropertyAtPath(path))
                                        continue;
                                    command::DependencyPropertyState property;
                                    if (!command::snapshotDependencyProperty(layer, path, property, error))
                                        break;
                                    item.properties.append(property);
                                }
                                if (!error.isEmpty())
                                    break;
                                state->items.append(item);
                            }
                            if (error.isEmpty()) {
                                for (const auto& mesh : meshes) {
                                    const auto specPath = state->target.MapToSpecPath(mesh.path);
                                    for (const auto& name : meshProperties) {
                                        const auto path = specPath.AppendProperty(name);
                                        if (layer->GetPropertyAtPath(path))
                                            command::removePropertySpec(layer, path);
                                    }
                                    const auto prim = stage->GetPrimAtPath(mesh.path);
                                    if (!prim.SetTypeName(TfToken("Mesh")) || !UsdGeomMesh(prim)) {
                                        error = "edit target cannot override the prim type";
                                        break;
                                    }
                                    UsdGeomMesh result(prim);
                                    bool ok = result.CreateFaceVertexCountsAttr().Set(
                                                  mesh.topology.GetFaceVertexCounts())
                                              && result.CreateFaceVertexIndicesAttr().Set(
                                                  mesh.topology.GetFaceVertexIndices())
                                              && result.CreateSubdivisionSchemeAttr().Set(UsdGeomTokens->none)
                                              && result.SetNormalsInterpolation(mesh.interpolation);
                                    for (const auto& sample : mesh.samples)
                                        ok = result.CreatePointsAttr().Set(sample.points, sample.time)
                                             && result.CreateNormalsAttr().Set(sample.normals, sample.time)
                                             && result.CreateExtentAttr().Set(sample.extent, sample.time) && ok;
                                    if (!ok) {
                                        error = "failed to author mesh geometry";
                                        break;
                                    }
                                    changed.append(mesh.path);
                                }
                                if (!error.isEmpty()) {
                                    QString rollback;
                                    if (!restore(layer, state->items, rollback))
                                        error += "; rollback failed: " + rollback;
                                    changed.clear();
                                    state->items.clear();
                                }
                            }
                        }
                    }
                }
            }
            command::queueToSession(session, [session, changed, error, undo]() {
                command::finishDeferred(session,
                                        error.isEmpty() ? (undo ? "Mesh conversion undone" : "Converted to mesh")
                                                        : command::appendError("Convert to mesh failed", error),
                                        changed,
                                        error.isEmpty() ? Session::Notify::Status::Success
                                                        : Session::Notify::Status::Error);
            });
        });
    };
    return Command([execute](Session* session) { execute(session, false); },
                   [execute](Session* session) { execute(session, true); });
}
}  // namespace stageviz
