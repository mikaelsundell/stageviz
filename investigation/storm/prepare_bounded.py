"""Prepare a clean USD 25.11 bounded-staging patch and Windows build overlay."""
from pathlib import Path
import difflib
import hashlib
import shutil

root = Path(r'C:\git\3rdparty\packages\OpenUSD-25.11')
base = Path(r'C:\Temp\usdtest\storm-investigation')
out = base/'bounded-staging'
overlay = Path(r'C:\git\3rdparty\patches\openusd')
out.mkdir(parents=True, exist_ok=True)
assert not (out/'pre-source').exists(), 'Already prepared; review existing work'
originals = {}
for relative in ('pxr/imaging/hdSt/stagingBuffer.cpp', 'pxr/imaging/hdSt/stagingBuffer.h',
                 'pxr/imaging/hdSt/indirectDrawBatch.cpp', 'pxr/imaging/hgiGL/ops.cpp',
                 'pxr/imaging/hgi/enums.h', 'pxr/imaging/hgiGL/buffer.cpp',
                 'pxr/imaging/hdSt/CMakeLists.txt'):
    path = root/relative
    backup = out/'pre-source'/relative
    backup.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(path, backup)
    saved = base/(path.name+'.original')
    if saved.exists():
        original = saved.read_text()
    elif (base/'upstream-upload/pre-source'/relative).exists():
        original = (base/'upstream-upload/pre-source'/relative).read_text()
    else:
        original = path.read_text()
    originals[relative] = original
    path.write_text(original)

relative = 'pxr/imaging/hdSt/stagingBuffer.cpp'
s = originals[relative]
s = s.replace('PXR_NAMESPACE_OPEN_SCOPE', '''#include "pxr/base/tf/envSetting.h"

#include <algorithm>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_ENV_SETTING(HDST_MAX_STAGING_BUFFER_SIZE, 64 * 1024 * 1024,
    "Maximum bytes in a staging batch or direct buffer upload (1..1 GiB)");

namespace {
size_t
_GetMaxStagingBufferSize()
{
    static const size_t limit = []() -> size_t {
        const int configured = TfGetEnvSetting(HDST_MAX_STAGING_BUFFER_SIZE);
        if (configured <= 0 || configured > 1024 * 1024 * 1024) {
            TF_WARN("Invalid HDST_MAX_STAGING_BUFFER_SIZE; using 64 MiB");
            return 64 * 1024 * 1024;
        }
        return static_cast<size_t>(configured);
    }();
    return limit;
}
} // anonymous namespace
''',1)
s = s.replace('    _capacity = capacity;',
              '    _capacity = std::min(capacity, _GetMaxStagingBufferSize());',1)
s = s.replace('''    if (!_isUma && copyOp.byteSize > queueThreshold) {
        HgiBlitCmds* blitCmds = _resourceRegistry->GetGlobalBlitCmds();
        blitCmds->CopyBufferCpuToGpu(copyOp);
        return;
    }

    HgiBufferHandle buffer''', '''    const size_t maxBatchSize = _GetMaxStagingBufferSize();
    if ((!_isUma && copyOp.byteSize > queueThreshold) ||
        copyOp.byteSize > maxBatchSize) {
        // Preserve the direct-upload fast path, but never issue a giant
        // individual transfer. Sources stay alive until registry submission.
        HgiBlitCmds* blitCmds = _resourceRegistry->GetGlobalBlitCmds();
        for (size_t offset = 0; offset < copyOp.byteSize;) {
            HgiBufferCpuToGpuOp chunk = copyOp;
            chunk.sourceByteOffset += offset;
            chunk.destinationByteOffset += offset;
            chunk.byteSize = std::min(maxBatchSize, copyOp.byteSize - offset);
            blitCmds->CopyBufferCpuToGpu(chunk);
            offset += chunk.byteSize;
        }
        return;
    }

    // Resize receives an estimate. Resolved sources may require more space
    // than estimated, so grow within the batch limit instead of dropping data.
    const size_t requiredCapacity = std::max(_capacity, copyOp.byteSize);
    if (_head > requiredCapacity - copyOp.byteSize ||
        requiredCapacity != _capacity) {
        Flush();
        // Flush only records commands. Complete their execution before
        // reusing CPU staging bytes or a UMA buffer consumed by those copies.
        _resourceRegistry->SubmitBlitWork(HgiSubmitWaitTypeWaitUntilCompleted);
    }
    _capacity = requiredCapacity;

    HgiBufferHandle buffer''',1)
assert 'const size_t maxBatchSize' in s
(root/relative).write_text(s)

relative = 'pxr/imaging/hdSt/stagingBuffer.h'
s = originals[relative]
old = '''    /// Set the capacity for the staging buffer.  Only applied once first call
    /// to StageCopy is called.
    /// Cannot be called if there have already been calls to StageCopy for this
    /// commit.'''
new = '''    /**
     * @brief Set the estimated upload size, capped to the staging batch limit.
     *
     * Allocation is deferred until StageCopy. Cannot be called with queued
     * copies. HDST_MAX_STAGING_BUFFER_SIZE defaults to 64 MiB.
     */'''
assert old in s
s = s.replace(old,new,1)
s = s.replace('''    /// Submit a CPU to GPU copy operation to be added to the staging buffer.
    /// The contents are memcpy'd over into the staging buffer during this call
    /// and a GPU to GPU blit is queued up to do the final copy to destination.''', '''    /**
     * @brief Queue a CPU to GPU copy using bounded staging or direct uploads.
     *
     * Full staging batches are flushed and completed before their memory is
     * reused. Oversized direct uploads are split at the same batch limit.
     */''',1)
(root/relative).write_text(s)

test_relative = 'pxr/imaging/hdSt/testenv/testHdStStagingBatches.cpp'
test = Path(__file__).with_name('testHdStStagingBatches.cpp').read_text()
(root/test_relative).write_text(test)
cmake_relative = 'pxr/imaging/hdSt/CMakeLists.txt'
cmake = originals[cmake_relative]
cmake += '''
# Bounded staging: exercise rollover, byte offsets and oversized direct copies.
pxr_build_test(testHdStStagingBatches
    LIBRARIES hdSt hgi garch tf
    CPPFILES testenv/testHdStStagingBatches.cpp
)
pxr_register_test(testHdStStagingBatches
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testHdStStagingBatches"
    EXPECTED_RETURN_CODE 0
    ENV HDST_MAX_STAGING_BUFFER_SIZE=4096
)
'''
(root/cmake_relative).write_text(cmake)

patch = []
manifest = []
changed = ['pxr/imaging/hdSt/stagingBuffer.cpp','pxr/imaging/hdSt/stagingBuffer.h',
           test_relative,cmake_relative]
for relative in changed:
    original = originals.get(relative,'')
    current = (root/relative).read_text()
    patch.extend(difflib.unified_diff(original.splitlines(True),current.splitlines(True),
        fromfile='a/'+relative if original else '/dev/null',tofile='b/'+relative))
    dest = overlay/'files'/relative
    dest.parent.mkdir(parents=True,exist_ok=True)
    dest.write_text(current)
    manifest.append({'path':relative,'original':hashlib.sha256(original.encode()).hexdigest(),
                     'patched':hashlib.sha256(current.encode()).hexdigest()})
(out/'bounded-staging.patch').write_text(''.join(patch))
overlay.mkdir(parents=True,exist_ok=True)
shutil.copy2(out/'bounded-staging.patch',overlay/'bounded-staging.patch')
import json
(overlay/'manifest.json').write_text(json.dumps(manifest,indent=2))
shutil.copy2(Path(__file__).with_name('apply_bounded_overlay.py'),overlay/'apply.py')
build = Path(r'C:\git\3rdparty\build_win.py')
build_text = build.read_text()
shutil.copy2(build,out/'build_win.py.original')
needle = '''    # Extract source
    if not os.path.exists(OPENUSD_DIR):
        unzip_file(OPENUSD_ARCHIVE, PACKAGES_DIR)
'''
assert build_text.count(needle)==1
build_text = build_text.replace(needle,needle+'''
    # Preserve the bounded Storm staging fix across clean archive builds.
    subprocess.run([sys.executable,
                    os.path.join(BASE_DIR, 'patches', 'openusd', 'apply.py'),
                    OPENUSD_DIR], check=True)
''',1)
build.write_text(build_text)
print('Clean bounded staging source, regression, overlay and Windows build step prepared')
