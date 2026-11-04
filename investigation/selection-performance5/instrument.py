"""Add opt-in aggregate scopes without changing selection/transform algorithms."""
from pathlib import Path
import re

root=Path(__file__).resolve().parents[2]
targets={
 'sources/rendering/viewportselection.cpp':[
  ('ViewportSelectionPrivate::sweepEvent','selection.sweep'),
  ('void build(const UsdStageRefPtr& stage)','selection.bvh_build'),
  ('std::vector<const Entry*> query(const GfFrustum& frustum) const','selection.bvh_query')],
 'sources/rendering/viewporttransform.cpp':[
  ('ViewportTransformPrivate::selectedVisiblePaths','transform.visible_selection'),
  ('ViewportTransformPrivate::selectionPivot','transform.selection_pivot'),
  ('ViewportTransformPrivate::beginDrag','transform.begin_drag'),
  ('ViewportTransformPrivate::updateDrag','transform.update_drag'),
  ('ViewportTransformPrivate::endDrag','transform.end_drag'),
  ('ViewportTransform::updateGeometry','transform.update_geometry')],
 'sources/core/selectionlist.cpp':[('SelectionList::updatePaths','selection.update_paths')],
 'sources/usd/primutils.cpp':[
  ('QList<SdfPath> objectPaths','transform.object_paths'),
  ('static QList<QPair<SdfPath, SdfPath>> transformInstanceSources','transform.instance_sources'),
  ('QList<SdfPath> transformEditTargetPaths','transform.resolve_targets'),
  ('QList<SdfPath> transformPreservationPaths','transform.preservation_paths')],
 'sources/outliner/outlinertree.cpp':[('OutlinerTreePrivate::updateSelection','ui.outliner_selection')],
 'sources/commands/commandtransform.cpp':[
  ('\nbeginTransformOperation','transform.capture_operation'),
  ('\nprepareTransforms','transform.prepare_authoring'),
  ('\napplyPreparedTransforms','transform.apply_prepared')],
 'sources/rendering/viewportgizmo.cpp':[('ViewportGizmoPrivate::updateTransformGeometry','transform.gizmo_geometry')]
}
for relative,entries in targets.items():
    path=root/relative;text=path.read_text(encoding='utf-8')
    if '#include "perfscope.h"' not in text:
        text=text.replace('#include ', '#include "perfscope.h"\n#include ',1)
    for function,label in entries:
        marker='PerfScope perfScope("'+label+'");'
        if marker in text:continue
        match=re.search(r'(?:^|\n)[ \t]*'+re.escape(function.lstrip('\n')),text)
        if not match:raise RuntimeError('Definition not found: '+function)
        start=match.start()
        brace=text.index('{',start)
        indent=re.match(r'[ \t]*',text[text.rfind('\n',0,brace)+1:brace]).group()+'    '
        text=text[:brace+1]+'\n'+indent+marker+text[brace+1:]
    path.write_text(text,encoding='utf-8')
path=root/'sources/commands/commandselection.cpp';text=path.read_text(encoding='utf-8')
if '#include "perfscope.h"' not in text:text=text.replace('#include ', '#include "perfscope.h"\n#include ',1)
start=text.index('\nselectInvert()');worker=text.index('command::runWorker([session, state]() {',start)
brace=text.index('{',worker)
if 'selection.invert_worker' not in text:
    text=text[:brace+1]+'\n                PerfScope perfScope("selection.invert_worker");'+text[brace+1:]
needle='domain = stage::leafPaths(stage, mask, false);'
pos=text.index(needle,start)
if 'selection.invert_domain' not in text:
    text=text[:pos]+'{\n                        PerfScope domainScope("selection.invert_domain");\n                        '+needle+'\n                    }'+text[pos+len(needle):]
path.write_text(text,encoding='utf-8')
path=root/'sources/rendering/renderengine.cpp';text=path.read_text(encoding='utf-8')
needle='if (!selectionCacheValid || cachedSelectionMappingGeneration != mappingGeneration) {'
if 'selection.hydra_mapping' not in text:
    text=text.replace(needle,needle+'\n        PerfScope perfScope("selection.hydra_mapping");',1)
path.write_text(text,encoding='utf-8')
print('Instrumented',len(targets)+2,'Stageviz source files')
