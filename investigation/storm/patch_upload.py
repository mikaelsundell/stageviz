from pathlib import Path
import difflib
root=Path(r'C:\git\3rdparty\packages\OpenUSD-25.11')
out=Path(r'C:\Temp\usdtest\storm-investigation')
here=Path(__file__).parent
def patch(relative, transform):
    path=root/relative
    original=path.read_text()
    backup=out/(path.name+'.original')
    if not backup.exists():backup.write_text(original)
    else:assert original==backup.read_text()
    text=transform(original)
    assert text!=original
    path.write_text(text)
    (out/(path.name+'.patch')).write_text(''.join(difflib.unified_diff(original.splitlines(True),text.splitlines(True),fromfile='a/'+relative,tofile='b/'+relative)))

def staging(s):
    s=s.replace('PXR_NAMESPACE_OPEN_SCOPE', '#include <fstream>\n#include <cstdlib>\n#include <cstring>\nPXR_NAMESPACE_OPEN_SCOPE\n'+(here/'staging_trace.h').read_text(),1)
    s=s.replace('    _capacity = capacity;', '    _capacity = capacity;\n    if (const char *dir=std::getenv("STORM_TRACE_DIR")) {\n        if (std::getenv("STORM_TRACE_PRIM")) {\n            std::ofstream reset(std::string(dir)+"/staging-targets.txt");\n        }\n    }',1)
    s=s.replace('        _head += copyOp.byteSize;', '        _StormTraceStaged(copyOp, _head, capacity, buffer);\n        _head += copyOp.byteSize;',1)
    return s
def ops(s):
    s=s.replace('PXR_NAMESPACE_OPEN_SCOPE', '#include <fstream>\n#include <cstdlib>\n#include <algorithm>\nPXR_NAMESPACE_OPEN_SCOPE\n'+(here/'hgigl_trace.h').read_text(),1)
    s=s.replace('                                 copyOp.byteSize);','                                 copyOp.byteSize);\n        _StormTraceGpuUpload(copyOp);',1)
    s=s.replace('            src);','            src);\n        _StormTraceCpuUpload(copyOp);',1)
    return s
patch('pxr/imaging/hdSt/stagingBuffer.cpp',staging)
patch('pxr/imaging/hgiGL/ops.cpp',ops)
print('Patched stagingBuffer.cpp and hgiGL/ops.cpp')
