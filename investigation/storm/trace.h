// Opt-in OpenUSD 25.11 diagnostic. Included inside the PXR namespace.
// Readback deliberately synchronizes GL and may change race/timing behavior.
namespace {
void _StormTrace(const char *phase, const void *batch,
    const std::vector<HdStDrawItemInstance const *> &items,
    const std::vector<uint32_t> &cpu, size_t stride,
    HdStDispatchBufferSharedPtr const &dispatch, bool gpuCulling)
{
    static const std::string target = TfGetenv("STORM_TRACE_PRIM");
    static const std::string directory = TfGetenv("STORM_TRACE_DIR");
    if (target.empty() || directory.empty() || !stride) return;
    static unsigned sequence = 0;
    for (size_t i = 0; i < items.size(); ++i) {
        const auto *item = items[i]->GetDrawItem();
        const std::string path = item->GetRprimID().GetString();
        // Scene delegates may prefix their own namespace to the USD path.
        if (path != target && !(path.size() > target.size() &&
            path.compare(path.size()-target.size(), target.size(), target)==0)) continue;
        if (sequence >= 240) return;
        const unsigned seq = sequence++;
        std::ofstream log(directory + "/storm-trace.txt", std::ios::app);
        log << "seq=" << seq << " phase=" << phase << " batch=" << batch
            << " prim=" << path << " command=" << i << " stride=" << stride
            << " gpuCulling=" << gpuCulling << " visible=" << items[i]->IsVisible();
        log << " cpu=";
        for (size_t j=0; j<stride && i*stride+j<cpu.size(); ++j) log << cpu[i*stride+j] << ',';
        if (std::string(phase)=="compile") { log << '\n'; continue; }
        glFinish();
        auto resource = dispatch ? dispatch->GetResource() : nullptr;
        if (resource && resource->GetHandle()) {
            GLuint id = static_cast<GLuint>(resource->GetHandle()->GetRawResource());
            GLint64 size=0;
            glGetNamedBufferParameteri64v(id, GL_BUFFER_SIZE, &size);
            std::vector<uint32_t> values(stride);
            size_t offset = resource->GetOffset()+i*stride*sizeof(uint32_t);
            log << " indirectBuffer=" << id << " actualBytes=" << size << " gpu=";
            if (offset+stride*sizeof(uint32_t)<=size_t(size)) {
                glGetNamedBufferSubData(id, offset, stride*sizeof(uint32_t), values.data());
                for (auto value: values) log << value << ',';
            } else log << "OUT_OF_BOUNDS";
        }
        GLint program=0, indirect=0, element=0, framebuffer=0, viewport[4]={};
        glGetIntegerv(GL_CURRENT_PROGRAM, &program);
        glGetIntegerv(GL_DRAW_INDIRECT_BUFFER_BINDING, &indirect);
        glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &element);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &framebuffer);
        glGetIntegerv(GL_VIEWPORT, viewport);
        log << " program=" << program << " boundIndirect=" << indirect << " boundElement=" << element
            << " framebuffer=" << framebuffer << " viewport=" << viewport[0] << ',' << viewport[1] << ',' << viewport[2] << ',' << viewport[3]
            << " depth=" << int(glIsEnabled(GL_DEPTH_TEST)) << " cullFace=" << int(glIsEnabled(GL_CULL_FACE)) << '\n';
        if (std::string(phase)=="submit") {
            const HdBufferArrayRangeSharedPtr bars[] = {item->GetVertexPrimvarRange(), item->GetTopologyRange(), item->GetFaceVaryingPrimvarRange(), item->GetConstantPrimvarRange()};
            const char *names[] = {"vertex", "topology", "fvar", "constant"};
            for (size_t b=0;b<4;++b) {
                auto bar=std::static_pointer_cast<HdStBufferArrayRange>(bars[b]);
                if (!bar) continue;
                for (const auto &entry: bar->GetResources()) {
                    auto r=entry.second;
                    if (!r || !r->GetHandle()) continue;
                    GLuint id=static_cast<GLuint>(r->GetHandle()->GetRawResource());
                    GLint64 size=0;
                    glGetNamedBufferParameteri64v(id, GL_BUFFER_SIZE, &size);
                    size_t step=r->GetStride() ? r->GetStride() : HdDataSizeOfTupleType(r->GetTupleType());
                    size_t offset=size_t(bar->GetElementOffset())*step+r->GetOffset();
                    size_t bytes=bar->GetNumElements()*step;
                    std::string file=std::to_string(seq)+"-"+names[b]+"-"+entry.first.GetString()+".bin";
                    log << " range=" << names[b] << " name=" << entry.first << " buffer=" << id
                        << " actualBytes=" << size << " resourceBytes=" << r->GetSize()
                        << " offset=" << offset << " elements=" << bar->GetNumElements()
                        << " step=" << step << " bytes=" << bytes;
                    if (bytes && bytes<=8*1024*1024 && offset+bytes<=size_t(size)) {
                        std::vector<char> data(bytes);
                        glGetNamedBufferSubData(id, offset, bytes, data.data());
                        std::ofstream out(directory+"/"+file, std::ios::binary);
                        out.write(data.data(), data.size());
                        log << " file=" << file;
                    } else log << " READBACK_SKIPPED";
                    log << '\n';
                }
            }
        }
        log << " glError=" << glGetError() << '\n';
    }
}
}
