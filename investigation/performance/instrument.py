from pathlib import Path
import difflib
import json
root=Path(r'C:\git\github\stageviz')
archive=Path(r'C:\git\3rdparty\packages\OpenUSD-25.11\stageviz-investigation\test3-performance')
specs={
 'sources/core/session.cpp':[
 ('SessionPrivate::loadFromFile(', 'session.load_file'),
 ('SessionPrivate::initStage()', 'session.init_stage'),
 ('SessionPrivate::boundingBox()', 'session.bounding_box'),
 ('SessionPrivate::updatePrims(', 'session.update_prims'),
 ('SessionPrivate::flushPrims()', 'session.flush_prims'),
 ('SessionPrivate::updateStage()', 'session.publish_stage')],
 'sources/commands/commandlayer.cpp':[],
 'sources/outliner/outlinerview.cpp':[
 ('OutlinerViewPrivate::primsChanged(', 'ui.outliner_prims'),
 ('OutlinerViewPrivate::stageChanged(', 'ui.outliner_stage')],
 'sources/layers/layertree.cpp':[('LayerTree::refresh()', 'ui.layer_tree_refresh')],
 'sources/rendering/viewportwidget.cpp':[('ViewportWidgetPrivate::paintGL()', 'viewport.paint')],
 'sources/rendering/renderengine.cpp':[('RenderEngine::Private::render()', 'hydra.render_cpu')],
}
diff=[]
for relative,functions in specs.items():
    path=root/relative
    original=path.read_text()
    assert '#include "perfscope.h"' not in original, relative
    s=original
    first=s.index('#include ')
    s=s[:first]+'#include "perfscope.h"\n'+s[first:]
    for signature,label in functions:
        start=s.index(signature)
        brace=s.index('{',start)
        s=s[:brace+1]+'\n    PerfScope perfScope("'+label+'");'+s[brace+1:]
    if relative.endswith('commandlayer.cpp'):
        needle='auto applyMuteState = [layerIdentifier, state](Session* session, bool muted) {'
        assert s.count(needle)==1
        s=s.replace(needle,needle+'\n        PerfScope perfScope("command.layer_mute");',1)
        s=s.replace('            stage->MuteLayer(identifier);','            { PerfScope usdScope("usd.mute_layer"); stage->MuteLayer(identifier); }',1)
        s=s.replace('            stage->UnmuteLayer(identifier);','            { PerfScope usdScope("usd.unmute_layer"); stage->UnmuteLayer(identifier); }',1)
    path.write_text(s)
    diff.extend(difflib.unified_diff(original.splitlines(True),s.splitlines(True),fromfile='a/'+relative,tofile='b/'+relative))
    dest=archive/'after'/'stageviz'/relative
    dest.parent.mkdir(parents=True,exist_ok=True)
    dest.write_text(s)
header=root/'sources/core/perfscope.h'
dest=archive/'after/stageviz/sources/core/perfscope.h'
dest.write_text(header.read_text())
(archive/'stageviz-performance.patch').write_text(''.join(diff))
(archive/'instrumented-files.json').write_text(json.dumps(list(specs)+['sources/core/perfscope.h'],indent=2))
print('Added opt-in CPU phase scopes; no application behavior changes')
