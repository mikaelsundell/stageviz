// Regression for bounded uploads, staging rollover and nonzero copy offsets.
#include "pxr/imaging/hdSt/resourceRegistry.h"
#include "pxr/imaging/hdSt/stagingBuffer.h"
#include "pxr/imaging/hgi/blitCmds.h"
#include "pxr/imaging/hgi/hgi.h"
#include "pxr/imaging/garch/glDebugWindow.h"
#include "pxr/base/tf/errorMark.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

int main(int argc, char **argv)
{
    TfErrorMark errors;
    GarchGLDebugWindow window("Bounded Storm staging regression", 64, 64);
    window.Init();
    auto hgi = Hgi::CreatePlatformDefaultHgi();
    if (!hgi) return 2;
    HdStResourceRegistry registry(hgi.get());
    HdStStagingBuffer staging(&registry);
    const char *setting = std::getenv("HDST_MAX_STAGING_BUFFER_SIZE");
    long long configured = setting ? std::strtoll(setting, nullptr, 10) : 64*1024*1024;
    size_t limit = configured > 0 && configured <= 1024*1024*1024
        ? size_t(configured) : size_t(64*1024*1024);
    const size_t total = 2*limit+137;
    std::vector<uint8_t> source(total+29), expected(total+32,0xcc);
    uint32_t state = 0x159ab2f;
    for (auto &byte : source) {
        state = state*1664525u+1013904223u;
        byte = uint8_t(state >> 24);
    }
    HgiBufferDesc desc;
    desc.debugName = "staging regression destination";
    desc.byteSize = expected.size();
    desc.initialData = expected.data();
    auto destination = hgi->CreateBuffer(desc);
    auto check = [&]() {
        staging.Flush();
        registry.SubmitBlitWork(HgiSubmitWaitTypeWaitUntilCompleted);
        std::vector<uint8_t> actual(expected.size());
        HgiBufferGpuToCpuOp read;
        read.gpuSourceBuffer = destination;
        read.byteSize = actual.size();
        read.cpuDestinationBuffer = actual.data();
        auto cmds = hgi->CreateBlitCmds();
        cmds->CopyBufferGpuToCpu(read);
        hgi->SubmitCmds(cmds.get(), HgiSubmitWaitTypeWaitUntilCompleted);
        return actual == expected;
    };
    // Deliberately huge estimate must not create a huge staging allocation.
    staging.Resize(size_t(12)*1024*1024*1024);
    for (size_t offset=0; offset<total;) {
        HgiBufferCpuToGpuOp copy;
        copy.cpuSourceBuffer = source.data();
        copy.sourceByteOffset = 17+offset;
        copy.gpuDestinationBuffer = destination;
        copy.destinationByteOffset = 11+offset;
        copy.byteSize = std::min({std::max(size_t(1),limit/3),size_t(128*1024),total-offset});
        staging.StageCopy(copy);
        std::copy_n(source.data()+17+offset,copy.byteSize,expected.data()+11+offset);
        offset += copy.byteSize;
    }
    if (!check()) { std::cerr << "rollover/offset readback failed\n"; return 3; }
    staging.Flush(); // Empty flush is harmless.
    // More than one batch in a single direct source, with prefix/suffix guards.
    staging.Resize(limit);
    HgiBufferCpuToGpuOp large;
    large.cpuSourceBuffer = source.data();
    large.sourceByteOffset = 19;
    large.gpuDestinationBuffer = destination;
    large.destinationByteOffset = 11;
    large.byteSize = limit+123;
    staging.StageCopy(large);
    std::copy_n(source.data()+19,large.byteSize,expected.data()+11);
    if (!check()) { std::cerr << "oversized direct readback failed\n"; return 4; }
    // Underestimated capacity and reuse after a completed submission.
    staging.Resize(0);
    large.byteSize = 17;
    large.sourceByteOffset = 3;
    staging.StageCopy(large);
    std::copy_n(source.data()+3,large.byteSize,expected.data()+11);
    if (!check()) { std::cerr << "underestimated capacity readback failed\n"; return 5; }
    HgiBufferCpuToGpuOp empty;
    staging.StageCopy(empty);
    staging.Flush();
    hgi->DestroyBuffer(&destination);
    if (!errors.IsClean()) return 6;
    std::cout << "PASS staging rollover, oversized direct uploads, offsets, guards, reuse and zero estimate; limit=" << limit << '\n';
    return 0;
}
