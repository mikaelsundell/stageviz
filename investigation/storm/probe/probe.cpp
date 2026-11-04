#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions_4_5_Core>
#include <QSurfaceFormat>
#include <array>
#include <cstdint>
#include <iostream>
#include <cstring>
#include <vector>

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QSurfaceFormat format;
    format.setVersion(4, 5);
    format.setProfile(QSurfaceFormat::CoreProfile);
    QOpenGLContext context;
    context.setFormat(format);
    if (!context.create()) return 2;
    QOffscreenSurface surface;
    surface.setFormat(context.format()); surface.create();
    if (!context.makeCurrent(&surface)) return 3;
    QOpenGLFunctions_4_5_Core gl;
    if (!gl.initializeOpenGLFunctions()) return 4;
    std::cout << "vendor=" << gl.glGetString(GL_VENDOR) << " renderer=" << gl.glGetString(GL_RENDERER)
        << " version=" << gl.glGetString(GL_VERSION) << std::endl;
    const uint64_t GiB=1024ull*1024*1024;
    std::array<uint32_t,16> expected;
    for (size_t i=0;i<expected.size();++i) expected[i]=0x12340000u+uint32_t(i);
    const uint64_t sizes[]={GiB,2*GiB+4096,4*GiB+4096,11501307132ull};
    const uint64_t offsets[]={0,GiB-128,2*GiB-128,2*GiB+128,3893442960ull,4*GiB+128};
    int failures=0;
    for (uint64_t bytes:sizes) {
        GLuint src=0,dst=0;
        gl.glCreateBuffers(1,&src); gl.glCreateBuffers(1,&dst);
        gl.glNamedBufferData(src,GLsizeiptr(bytes),nullptr,GL_STATIC_DRAW);
        gl.glNamedBufferData(dst,64,nullptr,GL_STATIC_DRAW);
        GLint64 actualSize=0; gl.glGetNamedBufferParameteri64v(src,GL_BUFFER_SIZE,&actualSize);
        std::cout << "allocation requested=" << bytes << " actual=" << actualSize << " error=" << gl.glGetError() << std::endl;
        for (uint64_t offset:offsets) {
            if (offset+64>bytes) continue;
            gl.glNamedBufferSubData(src,GLintptr(offset),64,expected.data()); gl.glFinish();
            std::array<uint32_t,16> source{},named{},bound{},zero{};
            gl.glGetNamedBufferSubData(src,GLintptr(offset),64,source.data());
            gl.glNamedBufferSubData(dst,0,64,zero.data());
            gl.glCopyNamedBufferSubData(src,dst,GLintptr(offset),0,64); gl.glFinish();
            GLenum namedError=gl.glGetError();
            gl.glGetNamedBufferSubData(dst,0,64,named.data());
            gl.glNamedBufferSubData(dst,0,64,zero.data());
            gl.glBindBuffer(GL_COPY_READ_BUFFER,src); gl.glBindBuffer(GL_COPY_WRITE_BUFFER,dst);
            gl.glCopyBufferSubData(GL_COPY_READ_BUFFER,GL_COPY_WRITE_BUFFER,GLintptr(offset),0,64); gl.glFinish();
            GLenum boundError=gl.glGetError();
            gl.glGetNamedBufferSubData(dst,0,64,bound.data());
            bool ok=source==expected && named==expected && bound==expected;
            if (!ok)++failures;
            std::cout << "copy allocation=" << bytes << " offset=" << offset
                << " sourceMatches=" << (source==expected) << " namedMatches=" << (named==expected)
                << " boundMatches=" << (bound==expected) << " namedError=" << namedError
                << " boundError=" << boundError << std::endl;
        }
        gl.glDeleteBuffers(1,&src); gl.glDeleteBuffers(1,&dst); gl.glFinish();
    }
    if (argc>1) {
        // Model the observed 4.50 GB upload into an 11.50 GB staging buffer,
        // followed by a small copy to a high offset in a 1.05 GB index buffer.
        const size_t uploadBytes=4495916560ull, sourceOffset=3893442960ull;
        const size_t destinationOffset=913783080ull;
        std::vector<char> cpu(uploadBytes,0);
        std::memcpy(cpu.data()+sourceOffset,expected.data(),64);
        GLuint src=0,dst=0;
        gl.glCreateBuffers(1,&src); gl.glCreateBuffers(1,&dst);
        gl.glNamedBufferData(src,11501307132ull,nullptr,GL_STATIC_DRAW);
        gl.glNamedBufferData(dst,1046166312ull,nullptr,GL_STATIC_DRAW);
        gl.glNamedBufferSubData(src,0,GLsizeiptr(cpu.size()),cpu.data()); gl.glFinish();
        std::array<uint32_t,16> source{},result{};
        gl.glGetNamedBufferSubData(src,GLintptr(sourceOffset),64,source.data());
        gl.glCopyNamedBufferSubData(src,dst,GLintptr(sourceOffset),GLintptr(destinationOffset),64); gl.glFinish();
        gl.glGetNamedBufferSubData(dst,GLintptr(destinationOffset),64,result.data());
        std::cout << "bulk-upload bytes=" << uploadBytes << " sourceMatches=" << (source==expected)
            << " destinationMatches=" << (result==expected) << " error=" << gl.glGetError() << std::endl;
        if (source!=expected || result!=expected)++failures;
        gl.glBindBuffer(GL_COPY_READ_BUFFER,src); gl.glBindBuffer(GL_COPY_WRITE_BUFFER,dst);
        gl.glCopyBufferSubData(GL_COPY_READ_BUFFER,GL_COPY_WRITE_BUFFER,GLintptr(sourceOffset),GLintptr(destinationOffset),64); gl.glFinish();
        gl.glGetNamedBufferSubData(dst,GLintptr(destinationOffset),64,result.data());
        std::cout << "bulk-bound-copy destinationMatches=" << (result==expected) << " error=" << gl.glGetError() << std::endl;
        // Re-upload exactly the same bytes in bounded calls.
        const size_t chunk=256ull*1024*1024;
        for (size_t offset=0; offset<cpu.size(); offset+=chunk) {
            size_t count=std::min(chunk,cpu.size()-offset);
            gl.glNamedBufferSubData(src,GLintptr(offset),GLsizeiptr(count),cpu.data()+offset);
        }
        gl.glFinish();
        gl.glCopyNamedBufferSubData(src,dst,GLintptr(sourceOffset),GLintptr(destinationOffset),64); gl.glFinish();
        gl.glGetNamedBufferSubData(dst,GLintptr(destinationOffset),64,result.data());
        std::cout << "chunked-upload chunk=" << chunk << " destinationMatches=" << (result==expected)
            << " error=" << gl.glGetError() << std::endl;
        gl.glDeleteBuffers(1,&src); gl.glDeleteBuffers(1,&dst); gl.glFinish();
    }
    std::cout << "failures=" << failures << std::endl;
    return failures ? 1:0;
}
