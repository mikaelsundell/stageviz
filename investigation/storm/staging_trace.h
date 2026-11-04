namespace {
void _StormTraceStaged(HgiBufferCpuToGpuOp const &op, size_t head,
                      size_t capacity, HgiBufferHandle const &buffer)
{
    const char *dir=std::getenv("STORM_TRACE_DIR");
    if (!dir || !std::getenv("STORM_TRACE_PRIM")) return;
    for (const char *name: {"points", "indices", "normals"}) {
        std::ifstream file(std::string(dir)+"/expected-"+name+".bin", std::ios::binary|std::ios::ate);
        if (!file || size_t(file.tellg())!=op.byteSize) continue;
        std::vector<char> expected(op.byteSize);
        file.seekg(0); file.read(expected.data(), expected.size());
        const char *source=static_cast<const char*>(op.cpuSourceBuffer)+op.sourceByteOffset;
        if (std::memcmp(expected.data(), source, expected.size())) continue;
        std::ofstream log(std::string(dir)+"/staging-targets.txt", std::ios::app);
        log << buffer->GetRawResource() << ' ' << head << ' '
            << op.gpuDestinationBuffer->GetRawResource() << ' ' << op.destinationByteOffset
            << ' ' << op.byteSize << ' ' << name << '\n';
        std::ofstream details(std::string(dir)+"/upload-trace.txt", std::ios::app);
        details << "stage name=" << name << " capacity=" << capacity << " head=" << head
            << " bytes=" << op.byteSize << " cpuMatchesExpected=1\n";
    }
}
}
