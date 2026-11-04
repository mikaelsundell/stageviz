from pathlib import Path
root=Path(r'C:\git\3rdparty\packages\OpenUSD-25.11')
path=root/'pxr/imaging/hdSt/stagingBuffer.cpp'
s=path.read_text()
marker="    // When the to-be-copied data is 'large'"
assert 'STORM_TRACE_BYPASS_STAGING' not in s
assert s.count(marker)==1
s=s.replace(marker, '''    // Diagnostic A/B control; not a proposed production fix.
    if (const char *bypass = std::getenv("STORM_TRACE_BYPASS_STAGING")) {
        if (std::string(bypass) == "1") {
            _resourceRegistry->GetGlobalBlitCmds()->CopyBufferCpuToGpu(copyOp);
            return;
        }
    }

'''+marker)
path.write_text(s)
print('Added opt-in direct upload comparison')
