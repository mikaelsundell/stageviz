"""Backport the OpenGL-relevant hunks of upstream 2a27479 to USD 25.11."""
from pathlib import Path
import difflib
import json

root = Path(r'C:\git\3rdparty\packages\OpenUSD-25.11')
out = Path(r'C:\Temp\usdtest\storm-investigation\upstream-upload')
out.mkdir(parents=True, exist_ok=True)
changes = {
    'pxr/imaging/hgi/enums.h': [
        ('/// <li>HgiBufferUsageCustomBitsBegin:',
         '/// <li>HgiBufferUsageUpload:\n///   Buffer will be used to upload data.</li>\n///\n/// <li>HgiBufferUsageCustomBitsBegin:'),
        ('    HgiBufferUsageIndirect = 1 << 4,',
         '    HgiBufferUsageIndirect = 1 << 4,\n    HgiBufferUsageUpload   = 1 << 5,'),
        ('    HgiBufferUsageCustomBitsBegin = 1 << 5,',
         '    HgiBufferUsageCustomBitsBegin = 1 << 6,'),
    ],
    'pxr/imaging/hdSt/stagingBuffer.cpp': [
        ('        bufferDesc.byteSize = _capacity;',
         '        bufferDesc.byteSize = _capacity;\n        bufferDesc.usage = HgiBufferUsageUpload;'),
    ],
    'pxr/imaging/hgiGL/buffer.cpp': [
        ('    glNamedBufferData(',
         '    GLenum usage = (desc.usage & HgiBufferUsageUpload)\n        ? GL_DYNAMIC_COPY : GL_STATIC_DRAW;\n\n    glNamedBufferData('),
        ('        GL_STATIC_DRAW);', '        usage);'),
    ],
}
patch = []
for relative, replacements in changes.items():
    path = root / relative
    original = path.read_text()
    assert 'HgiBufferUsageUpload' not in original, f'Already patched: {path}'
    text = original
    for before, after in replacements:
        assert text.count(before) == 1, (relative, before)
        text = text.replace(before, after, 1)
    backup = out / 'pre-source' / relative
    backup.parent.mkdir(parents=True, exist_ok=True)
    assert not backup.exists(), f'Backup already exists: {backup}'
    backup.write_text(original)
    path.write_text(text)
    patch.extend(difflib.unified_diff(original.splitlines(True), text.splitlines(True),
        fromfile='a/'+relative, tofile='b/'+relative))
(out/'backport.patch').write_text(''.join(patch))
(out/'provenance.json').write_text(json.dumps({
    'commit':'2a27479b97b30f610088d561c5a59ea180c7457d',
    'url':'https://github.com/PixarAnimationStudios/OpenUSD/commit/2a27479b97b30f610088d561c5a59ea180c7457d',
    'scope':'Hgi usage enum, Storm staging allocation, HgiGL allocation hint; Vulkan backend is disabled',
    'testControls':'STORM_TRACE_BYPASS_STAGING and STORM_TRACE_UPLOAD_CHUNK_MB both unset; normal GPU culling',
}, indent=2))
print('Applied upstream OpenGL allocation backport')
