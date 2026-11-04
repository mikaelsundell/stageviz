from pathlib import Path
import difflib
root = Path(r'C:\git\3rdparty\packages\OpenUSD-25.11')
source = root / 'pxr/imaging/hdSt/indirectDrawBatch.cpp'
out = Path(r'C:\Temp\usdtest\storm-investigation')
out.mkdir(parents=True, exist_ok=True)
original = source.read_text()
backup = out / 'indirectDrawBatch.cpp.original'
if not backup.exists():
    backup.write_text(original)
else:
    assert original == backup.read_text(), 'Source changed; review before repatching'
header = Path(__file__).with_name('trace.h').read_text()
text = original.replace('#include <iostream>', '#include <fstream>\n#include <iostream>')
text = text.replace('TF_DEFINE_PRIVATE_TOKENS(', header + '\nTF_DEFINE_PRIVATE_TOKENS(', 1)
text = text.replace('    // make sure we filled all', '    _StormTrace("compile", this, _drawItemInstances, _drawCommandBuffer, traits.numUInt32, _dispatchBuffer, _useGpuCulling);\n\n    // make sure we filled all', 1)
text = text.replace('    if (_useGpuCulling) {\n        // Ignore passed', '    _StormTrace("pre-cull", this, _drawItemInstances, _drawCommandBuffer, _dispatchBuffer->GetCommandNumUints(), _dispatchBuffer, _useGpuCulling);\n    if (_useGpuCulling) {\n        // Ignore passed', 1)
text = text.replace('                            renderPassState, resourceRegistry);\n    }\n}', '                            renderPassState, resourceRegistry);\n    }\n    _StormTrace("post-cull", this, _drawItemInstances, _drawCommandBuffer, _dispatchBuffer->GetCommandNumUints(), _dispatchBuffer, _useGpuCulling);\n}', 1)
text = text.replace('    uint32_t const drawCount = dispatchBuffer->GetCount();', '    uint32_t const drawCount = dispatchBuffer->GetCount();\n    _StormTrace("submit", this, _drawItemInstances, _drawCommandBuffer, dispatchBuffer->GetCommandNumUints(), dispatchBuffer, _useGpuCulling);', 1)
assert text.count('_StormTrace(') == 5
source.write_text(text)
(out / 'storm-tracing.patch').write_text(''.join(difflib.unified_diff(original.splitlines(True), text.splitlines(True), fromfile='a/pxr/imaging/hdSt/indirectDrawBatch.cpp', tofile='b/pxr/imaging/hdSt/indirectDrawBatch.cpp')))
print('Patched', source)
