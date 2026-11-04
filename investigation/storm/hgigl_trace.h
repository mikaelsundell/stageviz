namespace {
struct _StormUploadTarget {
    uint64_t src, srcOffset, dst, dstOffset, bytes;
    std::string name;
};
std::vector<_StormUploadTarget> _stormUploadTargets;
void _StormCheckUpload(const char *phase, GLuint id, size_t offset,
                       _StormUploadTarget const &target)
{
    const char *dir=std::getenv("STORM_TRACE_DIR");
    if (!dir) return;
    glFinish();
    std::vector<char> actual(target.bytes), expected(target.bytes);
    std::ifstream file(std::string(dir)+"/expected-"+target.name+".bin", std::ios::binary);
    file.read(expected.data(), expected.size());
    glGetNamedBufferSubData(id, offset, actual.size(), actual.data());
    std::ofstream log(std::string(dir)+"/upload-trace.txt", std::ios::app);
    log << phase << " name=" << target.name << " buffer=" << id << " offset=" << offset
        << " bytes=" << target.bytes << " matchesExpected=" << (actual==expected)
        << " allZero=" << std::all_of(actual.begin(), actual.end(), [](char c){return c==0;})
        << " error=" << glGetError() << '\n';
}
void _StormTraceCpuUpload(HgiBufferCpuToGpuOp const &op)
{
    const char *dir=std::getenv("STORM_TRACE_DIR");
    if (!dir || !std::getenv("STORM_TRACE_PRIM")) return;
    if (op.gpuDestinationBuffer->GetDescriptor().debugName!="HdStStagingBuffer") return;
    std::ofstream log(std::string(dir)+"/upload-trace.txt", std::ios::app);
    log << "staging-upload buffer=" << op.gpuDestinationBuffer->GetRawResource()
        << " bytes=" << op.byteSize << " sourceOffset=" << op.sourceByteOffset
        << " destinationOffset=" << op.destinationByteOffset << " error=" << glGetError() << '\n';
    log.close();
    _stormUploadTargets.clear();
    std::ifstream input(std::string(dir)+"/staging-targets.txt");
    _StormUploadTarget target;
    while (input >> target.src >> target.srcOffset >> target.dst >> target.dstOffset >> target.bytes >> target.name) {
        if (target.src != op.gpuDestinationBuffer->GetRawResource()) continue;
        if (target.srcOffset+target.bytes > op.destinationByteOffset+op.byteSize) continue;
        _stormUploadTargets.push_back(target);
        _StormCheckUpload("after-cpu-upload", GLuint(target.src), target.srcOffset, target);
    }
}
void _StormTraceGpuUpload(HgiBufferGpuToGpuOp const &op)
{
    for (const auto &target: _stormUploadTargets) {
        if (target.dst != op.gpuDestinationBuffer->GetRawResource() ||
            target.dstOffset<op.destinationByteOffset ||
            target.dstOffset+target.bytes>op.destinationByteOffset+op.byteSize) continue;
        _StormCheckUpload("before-gpu-copy-source", GLuint(op.gpuSourceBuffer->GetRawResource()),
            op.sourceByteOffset+target.dstOffset-op.destinationByteOffset, target);
        _StormCheckUpload("after-gpu-copy-destination", GLuint(target.dst), target.dstOffset, target);
    }
}
}
