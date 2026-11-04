from pathlib import Path
p=Path('CTest/regression.cpp')
s=p.read_text(encoding='utf-8')
at=s.index('void\nusdStageUtilsApi()')
s=s[:at]+'''void
indexedPathApi()
{
    const QList<SdfPath> roots { SdfPath("/A"), SdfPath("/A/B"), SdfPath("/C.attr"),
                               SdfPath("Relative"), SdfPath("/Variant{model=x}"), SdfPath() };
    const stageviz::path::PathIndex index(roots);
    for (const auto& path : { SdfPath("/A/B/C"), SdfPath("/AB"), SdfPath("/C"), SdfPath("/C.attr"),
                             SdfPath("Relative/Child"), SdfPath("Other"), SdfPath("/Variant{model=x}Child"),
                             SdfPath(), SdfPath("/") }) {
        bool covered = false;
        for (const auto& root : roots)
            covered = covered || (!root.IsEmpty() && path.HasPrefix(root));
        require(index.covers(path) == covered, "indexed coverage matches USD prefix semantics");
    }
    require(index.nearestAncestor(SdfPath("/A/B/C")) == SdfPath("/A/B"), "nearest indexed ancestor");
    require(index.outermostAncestor(SdfPath("/A/B/C")) == SdfPath("/A"), "outermost indexed ancestor");
    require(!stageviz::path::PathIndex().covers(SdfPath("/A")), "empty index covers nothing");
    require(stageviz::path::PathIndex({ SdfPath("/") }).covers(SdfPath("/A")), "absolute root covers prims");
    QList<SdfPath> inputs;
    for (int i = 0; i < 50000; ++i) {
        const SdfPath path("/World/P" + std::to_string(i) + "/Child");
        inputs.append(path);
        inputs.append(path);
    }
    inputs.append(SdfPath("/World"));
    require(stageviz::path::stableRootPaths(inputs) == QList<SdfPath> { SdfPath("/World") },
            "late ancestor reduces a large duplicate selection");
    inputs = { SdfPath("/B/Child"), SdfPath("/A"), SdfPath("/B"), SdfPath("/A"), SdfPath() };
    require(stageviz::path::stableRootPaths(inputs) == QList<SdfPath> { SdfPath("/A"), SdfPath("/B") },
            "stable roots retain surviving input order");
    QList<SdfPath> output { SdfPath("/A") };
    stageviz::path::PathCollector collector(output);
    for (const auto& path : inputs)
        collector.append(path);
    require(output == QList<SdfPath> { SdfPath("/A"), SdfPath("/B/Child"), SdfPath("/B") },
            "collector preserves initial values and insertion order");
}

void
batchedTransformAuthoring()
{
    const auto source = UsdStage::CreateInMemory();
    QList<SdfPath> paths;
    for (int i = 0; i < 1000; ++i) {
        const SdfPath path("/World/Assembly/P" + std::to_string(i));
        UsdGeomCube::Define(source, path);
        paths.append(path);
    }
    const auto stage = UsdStage::CreateInMemory();
    stage->GetRootLayer()->GetSubLayerPaths().push_back(source->GetRootLayer()->GetIdentifier());
    // An existing inert ancestor must survive; only our new Assembly over goes.
    SdfCreatePrimInLayer(stage->GetRootLayer(), SdfPath("/World"));
    std::string original;
    stage->GetRootLayer()->ExportToString(&original);
    stageviz::TransformOperation operation;
    QString error;
    require(stageviz::beginTransformOperation(stage, paths, operation, error), "capture composed transform targets");
    QStringList errors;
    const auto prepared = stageviz::prepareTransforms(stage, operation.paths, operation.edit.before, &errors);
    require(errors.isEmpty() && prepared.size() == paths.size(), "batch creates previously absent attributes");
    const GfMatrix4d delta = GfMatrix4d(1).SetTranslate(GfVec3d(3, 4, 5));
    require(stageviz::applyTransformOperation(stage, operation, prepared, delta, &errors), "batch preview applies");
    UsdGeomXformCache cache(UsdTimeCode::Default());
    for (const auto& path : paths)
        require(GfIsClose(cache.GetLocalToWorldTransform(stage->GetPrimAtPath(path)), delta, 1e-8),
                "all composed targets receive preview");
    require(stageviz::restoreTransformOperation(stage, operation, &errors), "batch undo restores opinions");
    std::string restored;
    stage->GetRootLayer()->ExportToString(&restored);
    require(restored == original, "batch undo removes new ancestors and retains preexisting inert overs");
    require(!source->GetPrimAtPath(paths.first()).GetAttribute(TfToken("xformOp:transform")),
            "weaker geometry remains unchanged");
}

''' +s[at:]
s=s.replace('{ "usd_path_utils_api", usdPathUtilsApi },','{ "usd_path_utils_api", usdPathUtilsApi },\n        { "indexed_path_api", indexedPathApi },\n        { "batched_transform_authoring", batchedTransformAuthoring },')
p.write_text(s,encoding='utf-8')
p=Path('CTest/CMakeLists.txt');s=p.read_text(encoding='utf-8').replace('    usd_path_utils_api\n','    usd_path_utils_api\n    indexed_path_api\n    batched_transform_authoring\n');p.write_text(s,encoding='utf-8')
