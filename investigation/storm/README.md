# Storm missing-geometry investigation

Current production fix: bounded 64 MiB staging is enabled by default in the
installed third-party Release tree. See `C:\git\3rdparty\patches\openusd\README.md`
and `C:\Temp\usdtest\storm-investigation\bounded-staging\README.txt` for the
changed-file list, future Windows build integration, and verification.
The diagnostic experiments and bypass instructions below are historical.

Results and binary artifacts live in `C:\Temp\usdtest\storm-investigation`.
See `REPORT.txt` there for the measured results and limitations.

The supplied OpenUSD 25.11 archive is built in
`C:\git\3rdparty\packages\OpenUSD-25.11\build\vc143\x86_amd64\Release`.
Only `usd_hdSt.dll` and `usd_hgiGL.dll` were replaced in the requested
Stageviz release. Their original copies are preserved in `original-dlls`.
The original `TEMP_asm.usd` was never saved. An all-visible wrapper is in
`run01\visible-wrapper.usda` and sublayers the original file by absolute path.

All instrumentation is opt-in. `STORM_TRACE_PRIM` selects the exact USD path;
`STORM_TRACE_DIR` selects an existing output directory. GPU command readback
is capped at 240 target checkpoints. Geometry readbacks are limited to the
target BAR ranges and at most 8 MiB per resource. Synchronization and readback
can change timing. Constant-resource dumps contain interleaved records;
points, indices and normals are compared with evaluated USD data.

`STORM_TRACE_BYPASS_STAGING=1` sends uploads directly to their final buffers.
This is the full-scene diagnostic workaround that restored the assembly.
`STORM_TRACE_UPLOAD_CHUNK_MB=256` is a separate experiment: it fixes the
reduced GL probe, but the full scene crashed in `nvoglv64.dll`; it is not the
recommended launch configuration.

`controller.py` is an external MCP client for `http://127.0.0.1:37651/mcp`.
Never run it from embedded Stageviz Python. The other Python scripts are
executed through the MCP `stageviz_run_python` tool. That tool clears undo
history. Load operations are asynchronous: wait for the scene to settle and
check stage info before capturing or authoring.

`run-patched.ps1` launches the installed diagnostic DLLs with staging bypassed
and the all-visible wrapper. Run only one large-scene test at a time. The script
does not replace DLLs or save the dataset.

Source patch preparation, in order on an unmodified archive:

1. `patch_usd.py` adds per-prim draw tracing and saves source originals.
2. `patch_upload.py` adds staging/upload tracing.
3. `patch_bypass.py` adds the direct-upload comparison.
4. `patch_chunks.py` adds the separate chunked-upload experiment.

These scripts intentionally reject already patched input. The exported unified
patch records the final changes. No Stageviz production sources were changed.

Build the standalone probe with CMake and Qt6 Gui/OpenGL from the existing
third-party prefix. Running without arguments checks sparse writes and copies
around 2/4 GiB offsets. `--bulk` additionally reproduces the large-upload failure,
tests bound copies, and retries with 256 MiB upload calls. Exit code 1 is expected
on this machine because the original bulk case fails; inspect the per-case output.

Before restoring DLLs, close the test Stageviz process, then copy the two matching
files from `original-dlls` back to the Stageviz release directory. Do not mix the
original and diagnostic copies when comparing builds.
