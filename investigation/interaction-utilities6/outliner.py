from pathlib import Path
p=Path('sources/outliner/outlinertree.cpp');s=p.read_text(encoding='utf-8')
start=s.index('    for (qsizetype i = effectiveRemaps.size() - 1;',s.index('OutlinerTreePrivate::updateOverrideCacheForNamespaceChanges'))
end=s.index('\n    QSet<SdfPath> affectedPaths;',start)
s=s[:start]+'''    QList<QPair<SdfPath, SdfPath>> minimalRemaps;
    path::PathIndex remappedRoots;
    QHash<SdfPath, SdfPath> remapDestinations;
    for (const auto& remap : effectiveRemaps) {
        if (!remappedRoots.covers(remap.first)) {
            minimalRemaps.append(remap);
            remappedRoots.insert(remap.first);
            remapDestinations.insert(remap.first, remap.second);
        }
    }
    effectiveRemaps = std::move(minimalRemaps);
    const QList<SdfPath> effectiveRemovedPaths = path::minimalRootPaths(removedPaths);
    const path::PathIndex removedRoots(effectiveRemovedPaths);
''' +s[end:]
start=s.index('            bool isRemoved = false;',s.index('OutlinerTreePrivate::updateOverrideCacheForNamespaceChanges'))
end=s.index('\n        }\n\n        for (const SdfPath& path : removed)',start)
s=s[:start]+'''            if (removedRoots.covers(path)) {
                removed.append(path);
                continue;
            }
            const SdfPath source = remappedRoots.nearestAncestor(path);
            if (!source.IsEmpty()) {
                removed.append(path);
                added.append(path.ReplacePrefix(source, remapDestinations.value(source)));
            }''' +s[end:]
p.write_text(s,encoding='utf-8')
