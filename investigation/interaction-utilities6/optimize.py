"""Apply the reviewed loop migrations; retained as an investigation recipe."""
from pathlib import Path

def edit(name, old, new):
    p = Path(name)
    text = p.read_text(encoding='utf-8')
    assert old in text, (name, old[:100])
    p.write_text(text.replace(old, new), encoding='utf-8')

p='sources/commands/commandselection.cpp'
for selection in ('previousSelection', 'selectedPayloads'):
    edit(p, f'for (const SdfPath& path : domain) {{\n                    if (!path::isCoveredByRoots({selection}, path))',
         f'const path::PathIndex selectedRoots({selection});\n                for (const SdfPath& path : domain) {{\n                    if (!selectedRoots.covers(path))')

p='sources/usd/primutils.cpp'
a=Path(p).read_text(encoding='utf-8')
start=a.index('        const auto sources = transformInstanceSources(stage);', a.index('    QList<SdfPath> transformEditTargetPaths'))
end=a.index('\n        return targets;',start)
a=a[:start]+'''        QList<SdfPath> sourcePaths;
        for (const auto& source : transformInstanceSources(stage))
            sourcePaths.append(source.first);
        const path::PathIndex sourceRoots(sourcePaths);
        path::PathCollector collector(targets);
        for (const SdfPath& path : objectPaths(stage, paths)) {
            SdfPath targetPath = transformProxyTargetPath(stage, path);
            const SdfPath source = sourceRoots.outermostAncestor(targetPath);
            if (!source.IsEmpty())
                targetPath = source;
            collector.append(targetPath);
        }
        if (minimalRoots)
            return path::stableRootPaths(targets);
''' +a[end:]
start=a.index('        QList<SdfPath> preserved;',a.index('    QList<SdfPath> transformPreservationPaths'))
end=a.index('\n        return preserved;',start)
a=a[:start]+'''        QList<SdfPath> preserved;
        const path::PathIndex selected(targets);
        path::PathCollector collector(preserved);
        for (const auto& source : transformInstanceSources(stage)) {
            if (selected.contains(source.first) && !selected.covers(source.second))
                collector.append(source.second);
        }'''+a[end:]
start=a.index('    QList<SdfPath> leafPaths(')
end=a.index('    QList<SdfPath> visiblePaths(',start)
part=a[start:end].replace('        for (const UsdPrim& prim : stage->Traverse()) {','        const path::PathIndex maskRoots(mask);\n        for (const UsdPrim& prim : stage->Traverse()) {').replace('!path::isWithinRoots(mask, path)','!mask.isEmpty() && !maskRoots.covers(path)').replace('path::isWithinRoots(mask, child.GetPath())','mask.isEmpty() || maskRoots.covers(child.GetPath())')
a=a[:start]+part+a[end:]
Path(p).write_text(a,encoding='utf-8')

p='sources/rendering/viewporttransform.cpp'
edit(p,'    paths.reserve(d.host->selectedPaths().size());','    paths.reserve(d.host->selectedPaths().size());\n    path::PathCollector collector(paths);')
edit(p,'        if (!paths.contains(path))\n            paths.append(path);','        collector.append(path);')
edit(p,'    const QList<SdfPath> targetPaths = visibleTargetPaths();','    const QList<SdfPath> targetPaths = stage::transformEditTargetPaths(d.host->stage(), selectedPaths);')

p='sources/commands/commandtransform.cpp'
edit(p,'    // Every input must resolve before any matrix/order override is authored.','    const path::PathIndex targetRoots(resolved.paths);\n    // Every input must resolve before any matrix/order override is authored.')
edit(p,'''        bool covered = false;
        for (const SdfPath& target : resolved.paths)
            covered = covered || primPath.HasPrefix(target);
        if (!covered) {''','''        if (!targetRoots.covers(primPath)) {''')
edit(p,'    for (const SdfPath& path : resolved.paths) {\n        GfMatrix4d world(1.0);\n        if (!stage::worldTransform(stage, path, world, error))\n            return false;', '''    UsdGeomXformCache worldCache(UsdTimeCode::Default());
    for (const SdfPath& path : resolved.paths) {
        const GfMatrix4d world = worldCache.GetLocalToWorldTransform(stage->GetPrimAtPath(path));''')
