# Stageviz performance benchmarks

These scripts are manual performance tools, not CTest cases. They load a
generated USDA fixture into the active session and replace its current stage.
Run them only in a disposable Stageviz process without unsaved user work.

## Run through MCP

Start a debug build with the MCP server enabled:

```sh
DYLD_IMAGE_SUFFIX=_debug build.debug/Debug/stageviz.app/Contents/MacOS/stageviz \
  --execute 'stageviz.startMcpServer()'
```

Use the external MCP client to call `stageviz_run_python` with:

```python
import runpy
runpy.run_path("/absolute/path/to/stageviz/CTest/benchmark/benchmark_prim_commands.py")
```

Each script creates fixtures, waits for deferred session updates to finish,
checks the resulting USD state, and records dispatch and end-to-end times. The
scripts use `testbench` for Qt event processing and viewer screenshots. They
write JSON reports and PNG screenshots under the system temporary directory.

Set `STAGEVIZ_BENCHMARK_COUNT` before launching Stageviz to change the default
fixture size (5,000 prims). `benchmark_hide_delete.py` also accepts
`STAGEVIZ_BENCHMARK_OPERATION=hide` or `delete`.

## Scripts

- `benchmark_prim_commands.py`: selected hide/show, subtree rename, multi-prim
  move, and delete.
- `benchmark_outliner_parent_commands.py`: rename, move, and delete a parent
  with a large expanded Outliner subtree; checks both USD and Qt row counts.
- `benchmark_hide_delete.py`: focused hide or delete run for a large selection.

The tests must use MCP for live app operation and capture screenshots through
the viewer widget. Do not add these scripts to the automatic CTest suite.
