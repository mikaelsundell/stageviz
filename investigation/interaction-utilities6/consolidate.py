from pathlib import Path
p=Path('sources/usd/usdutils.cpp');s=p.read_text(encoding='utf-8')
start=s.index('    using PathSet =')
end=s.index('    QList<SdfPath> uniquePaths',start)
s=s[:start]+'''    QList<SdfPath> topLevelPaths(const QList<SdfPath>& paths)
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

''' +s[end:]
start=s.index('    QList<SdfPath> uniquePaths')
end=s.index('    QList<SdfPath> minimalRootPaths',start)
s=s[:start]+'''    QList<SdfPath> uniquePaths(const QList<SdfPath>& paths)
    {
        QList<SdfPath> result;
        result.reserve(paths.size());
        PathCollector collector(result);
        for (const auto& path : paths)
            collector.append(path);
        return result;
    }

''' +s[end:]
s=s.replace('        PathSet accepted;\n        accepted.reserve(static_cast<size_t>(sorted.size()));','        PathIndex accepted;').replace('isCoveredBySet(accepted, path)','accepted.covers(path)')
s=s.replace('        PathSet removedRoots;\n        removedRoots.reserve(static_cast<size_t>(removedPaths.size()));','        PathIndex removedRoots;').replace('isCoveredBySet(removedRoots, primPath)','removedRoots.covers(primPath)')
p.write_text(s,encoding='utf-8')
