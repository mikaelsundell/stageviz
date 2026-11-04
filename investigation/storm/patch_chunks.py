from pathlib import Path
path=Path(r'C:\git\3rdparty\packages\OpenUSD-25.11\pxr\imaging\hgiGL\ops.cpp')
s=path.read_text()
old='''        glNamedBufferSubData(
            glBuffer->GetBufferId(),
            dstOffset,
            copyOp.byteSize,
            src);'''
new='''        // Diagnostic comparison: preserve staging, bound individual uploads.
        size_t chunkBytes = copyOp.byteSize;
        if (const char *value = std::getenv("STORM_TRACE_UPLOAD_CHUNK_MB")) {
            size_t mb = std::strtoull(value, nullptr, 10);
            if (mb > 0 && mb <= 1024) chunkBytes = mb * 1024 * 1024;
        }
        for (size_t offset = 0; offset < copyOp.byteSize;) {
            size_t count = std::min(chunkBytes, copyOp.byteSize-offset);
            glNamedBufferSubData(glBuffer->GetBufferId(),
                dstOffset+offset, count, src+offset);
            offset += count;
        }'''
assert s.count(old)==1
path.write_text(s.replace(old,new))
print('Added opt-in bounded uploads')
