# SPDX-License-Identifier: BSD-3-Clause
"""Add the interaction branch's optional timing probes to an isolated baseline."""
import argparse
from pathlib import Path
import shutil
import subprocess


def instrument(root, current):
    root, current = Path(root), Path(current)
    shutil.copyfile(current / 'sources/core/perfscope.h', root / 'sources/core/perfscope.h')
    hooks = {
        'sources/rendering/viewportselection.cpp': [
            ('void build(const UsdStageRefPtr& stage)', 'selection.bvh_build'),
            ('std::vector<const Entry*> query(const GfFrustum& frustum) const', 'selection.bvh_query'),
            ('ViewportSelectionPrivate::sweepEvent(', 'selection.sweep')],
        'sources/rendering/viewportwidget.cpp': [
            ('ViewportWidgetPrivate::paintGL()', 'viewport.paint'),
            ('ViewportWidgetPrivate::updateSelection(', 'ui.viewport_selection'),
            ('ViewportWidgetPrivate::updateSceneStats()', 'ui.scene_statistics')],
        'sources/outliner/outlinertree.cpp': [('OutlinerTreePrivate::updateSelection(', 'ui.outliner_selection')],
        'sources/properties/propertytree.cpp': [('PropertyTreePrivate::updateSelection(', 'ui.property_selection')],
        'sources/rendering/renderengine.cpp': [('RenderEngine::Private::render()', 'hydra.render_cpu')],
        'sources/rendering/viewporttransform.cpp': [
            ('ViewportTransformPrivate::selectedVisiblePaths() const', 'transform.visible_selection'),
            ('ViewportTransformPrivate::selectionPivot(', 'transform.selection_pivot'),
            ('ViewportTransformPrivate::beginDrag(', 'transform.begin_drag'),
            ('ViewportTransformPrivate::updateDrag(', 'transform.update_drag'),
            ('ViewportTransformPrivate::endDrag()', 'transform.end_drag')],
        'sources/commands/commandtransform.cpp': [
            ('beginTransformOperation(', 'transform.capture_operation'),
            ('prepareTransforms(', 'transform.prepare_authoring'),
            ('applyPreparedTransforms(', 'transform.apply_prepared'),
            ('restoreTransformOperation(', 'transform.restore_operation')],
        'sources/core/session.cpp': [
            ('SessionPrivate::initStage()', 'session.init_stage'),
            ('SessionPrivate::loadFromFile(', 'session.load_file'),
            ('SessionPrivate::boundingBox()', 'session.bounding_box'),
            ('SessionPrivate::updatePrims(', 'session.update_prims'),
            ('SessionPrivate::flushPrims()', 'session.flush_prims'),
            ('SessionPrivate::updateStage()', 'session.publish_stage')],
    }
    for name, functions in hooks.items():
        path = root / name
        text = path.read_text()
        text = '#include "perfscope.h"\n' + text
        for signature, label in functions:
            # Locate definitions, not earlier call sites or class declarations.
            candidates = []
            offset = 0
            while True:
                start = text.find(signature, offset)
                if start < 0:
                    break
                brace = text.find('{', start)
                semicolon = text.find(';', start)
                if brace >= 0 and (semicolon < 0 or brace < semicolon):
                    candidates.append(brace)
                offset = start + len(signature)
            assert len(candidates) == 1, (name, signature, candidates)
            index = candidates[0] + 1
            text = text[:index] + '\n    PerfScope perfScope("' + label + '");' + text[index:]
        if name.endswith('renderengine.cpp'):
            marker = 'if (!selectionCacheValid || cachedSelectionMappingGeneration != mappingGeneration) {'
            assert text.count(marker) == 1
            text = text.replace(marker, marker + '\n        PerfScope mappingScope("selection.hydra_mapping");')
        if name.endswith('viewporttransform.cpp'):
            text = '#include <QVariant>\n' + text
            start = text.index('ViewportTransformPrivate::prepareFrame()')
            end = text.index('\n}\n', start)
            body = text[start:end]
            before = '    return true;'
            assert body.count(before) == 1
            body = body.replace(before, '''    if (PerfScope::enabled()) {
        QPointF screen;
        const bool projected = d.frame.pivotValid && d.host->projectWorldToScreen(d.frame.pivot, screen);
        d.host->widget()->setProperty("transformPivotValid", projected);
        d.host->widget()->setProperty("transformPivotScreen", screen);
    }
''' + before)
            text = text[:start] + body + text[end:]
            text = text.replace('    return p->beginDrag(pos, copy);', '''    const bool started = p->beginDrag(pos, copy);
    if (PerfScope::enabled()) {
        p->d.host->widget()->setProperty("transformDragging", p->d.dragging);
        p->d.host->widget()->setProperty("transformActiveAxis", p->d.frame.activeAxis);
        p->d.host->widget()->setProperty("transformTargetCount", p->d.operation.targetCount);
    }
    return started;''')
            text = text.replace('    p->endDrag();', '''    p->endDrag();
    if (PerfScope::enabled())
        p->d.host->widget()->setProperty("transformDragging", p->d.dragging);''')
        path.write_text(text)
    print(subprocess.check_output(['git', '-C', str(root), 'diff', '--stat'], text=True))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('baseline')
    parser.add_argument('current')
    args = parser.parse_args()
    instrument(args.baseline, args.current)
