from pathlib import Path
p=Path('sources/rendering/viewportwidget.cpp');s=p.read_text(encoding='utf-8')
s=s.replace('        ViewportSceneCounts cachedSceneTotals;','        ViewportSceneCounts cachedSceneTotals;\n        ViewportSceneCounts cachedSelectedTotals;\n        bool selectedTotalsDirty = true;')
s=s.replace('d.selection = paths;','d.selection = paths;\n        d.selectedTotalsDirty = true;',1)
start=s.index('ViewportWidgetPrivate::updateSelection(')
head=s[:start];tail=s[start:].replace('    d.selection = paths;','    d.selection = paths;\n    d.selectedTotalsDirty = true;',1);s=head+tail
s=s.replace('    d.cachedSceneTotals = {};','    d.cachedSceneTotals = {};\n    d.cachedSelectedTotals = {};\n    d.selectedTotalsDirty = true;')
s=s.replace('    d.sceneTotalsDirty = d.sceneTotalsDirty || !notice::onlyTransformProperties(batch);','    const bool countsChanged = !notice::onlyTransformProperties(batch);\n    d.sceneTotalsDirty = d.sceneTotalsDirty || countsChanged;\n    d.selectedTotalsDirty = d.selectedTotalsDirty || countsChanged;')
start=s.index('    auto filterRootPaths =',s.index('ViewportWidgetPrivate::updateSceneStats()'))
end=s.index('    if (d.stage) {',start)
s=s[:start]+'    ViewportSceneCounts selected = d.cachedSelectedTotals;\n'+s[end:]
s=s.replace('''        if (!d.selection.isEmpty()) {
            const QList<SdfPath> roots = filterRootPaths(d.selection);''','''        if (d.selectedTotalsDirty) {
            selected = {};
            const QList<SdfPath> roots = path::minimalRootPaths(d.selection);''')
needle='''    const ViewportSceneCounts& total = d.cachedSceneTotals;'''
s=s.replace(needle,'''    d.cachedSelectedTotals = selected;
    d.selectedTotalsDirty = false;
'''+needle)
p.write_text(s,encoding='utf-8')
