from pathlib import Path
import difflib
import hashlib
import json
import zipfile

source=Path(r'C:\git\3rdparty\packages\OpenUSD-25.11')
overlay=Path(r'C:\git\3rdparty\patches\openusd')
out=Path(r'C:\Temp\usdtest\storm-investigation\bounded-staging')
path=source/'pxr/imaging/hdSt/stagingBuffer.cpp'
s=path.read_text()
old='''    if ((!_isUma && copyOp.byteSize > queueThreshold) ||
        copyOp.byteSize > maxBatchSize) {'''
new='''    if (_isUma && copyOp.byteSize > maxBatchSize) {
        // UMA must keep CPU writes in owned staging memory until the GPU
        // consumes them. Split oversized sources without writing directly
        // into a final buffer that an earlier frame may still be reading.
        for (size_t offset = 0; offset < copyOp.byteSize;) {
            HgiBufferCpuToGpuOp chunk = copyOp;
            chunk.sourceByteOffset += offset;
            chunk.destinationByteOffset += offset;
            chunk.byteSize = std::min(maxBatchSize, copyOp.byteSize - offset);
            StageCopy(chunk);
            offset += chunk.byteSize;
        }
        return;
    }
    if (!_isUma && copyOp.byteSize > queueThreshold) {'''
assert s.count(old)==1
path.write_text(s.replace(old,new,1))
# Exercise aggregation within each deliberately tiny staging batch as well.
test_path=source/'pxr/imaging/hdSt/testenv/testHdStStagingBatches.cpp'
s=test_path.read_text().replace('copy.byteSize = std::min({limit,size_t(128*1024),total-offset});',
    'copy.byteSize = std::min({std::max(size_t(1),limit/3),size_t(128*1024),total-offset});')
test_path.write_text(s)
entries=json.loads((overlay/'manifest.json').read_text())
patch=[]
with zipfile.ZipFile(r'C:\git\3rdparty\packages\OpenUSD-25.11.zip') as archive:
    for entry in entries:
        relative=entry['path']
        names=[name for name in archive.namelist() if name.endswith('/'+relative)]
        original=archive.read(names[0]).decode().replace('\r\n','\n') if names else ''
        current=(source/relative).read_text()
        assert hashlib.sha256(original.encode()).hexdigest()==entry['original']
        entry['patched']=hashlib.sha256(current.encode()).hexdigest()
        (overlay/'files'/relative).write_text(current)
        patch.extend(difflib.unified_diff(original.splitlines(True),current.splitlines(True),
            fromfile='a/'+relative if original else '/dev/null',tofile='b/'+relative))
(overlay/'manifest.json').write_text(json.dumps(entries,indent=2))
for destination in (overlay/'bounded-staging.patch',out/'bounded-staging.patch'):
    destination.write_text(''.join(patch))
print('Preserved UMA staging ownership and added within-batch aggregation coverage')
