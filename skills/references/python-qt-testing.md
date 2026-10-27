# Testing Qt in Stageviz

Use this reference when writing or debugging Python tests that inspect Stageviz widgets, dialogs, actions, or the presentation of USD/session state.

## Choose the right test layer

- Put critical C++ command and session behavior in `CTest/regression.cpp`; put widget and signal behavior that can run in the GUI/offscreen test target in `CTest/gui_regression.cpp`.
- For workflows exposed to Stageviz's embedded Python interpreter, add or extend `CTest/python/test*.py`. Reuse `CTest/python/testbench.py` instead of building new Qt discovery, waiting, snapshot, and USD fixture helpers in each script.
- Keep a focused test directly runnable from Stageviz, and include it in `CTest/python/test.py` when it belongs to the aggregate Python suite.
- Use Stageviz command bindings for live-stage changes so undo/redo is exercised. Direct `pxr` authoring is appropriate for preparing isolated fixture files before loading them.
- Establish a known fixture and undo-history baseline at the start of the test. Then perform the changes under test through commands and verify undo and redo without resetting history between those operations. Running `stageviz_run_python` through MCP clears the native undo/redo history; account for that when choosing the test entry point.

## Drive Qt through QObject identity

- Find actions and widgets by stable `objectName` values, use model roles and properties for state, and scope searches to the active visible dialog when duplicate names can occur. Avoid screen-coordinate clicks or labels as the primary selector.
- Enumerate with `QApplication.topLevelWidgets()` and `QApplication.allWidgets()` static methods. In some PySide builds, `QApplication.instance()` is exposed as a `QCoreApplication` wrapper that does not provide those widget enumeration methods.
- Trigger the current live `QAction` by its object name; do not cache an action wrapper across UI reconstruction. Verify both the action's enabled state and the resulting behavior.
- For Layer Editor controls, resolve the visible `layerDialog` first and search within it. A viewer-wide lookup can find a hidden or stale copy of a same-named control.
- Assert both sides of the behavior: query USD/session state, then check that the corresponding Qt row, icon/decor role, tooltip, enabled state, dirty marker, selection, or expansion state presents it correctly.

## Handle Qt object lifetimes and asynchronous refresh

- Treat PySide wrappers as short-lived whenever events can rebuild a stage, dialog, tree, or model. Reacquire the active widget after stage loads, save/retarget operations, `processEvents`, timers, or dialog replacement; validate it with a native method call such as `tree.model()` before use.
- Do not retain Qt wrappers in snapshots, cached test state, or predicates that may run after the owning widget is deleted. Store plain Python values such as names, booleans, row text, identifiers, and icon/model-role data instead.
- Use `testbench.wait_until()` for state that arrives after queued Qt refreshes. A `RuntimeError` from a wrapper whose C++ object was deleted is a lifecycle signal: reacquire the current object and retry within a bounded wait, rather than continuing to use that wrapper.
- For status-bar output, connect to `QStatusBar.messageChanged` before triggering the action and collect the emitted plain strings. A previously found status-bar wrapper can be deleted during refresh, and reading `currentMessage()` afterward may be stale or miss a transient message.
- Treat command progress dialogs as optional and transient when commands execute synchronously through Python bindings. Assert command completion and resulting USD/UI state; verify a progress dialog only when the implementation actually presents one.

## Capture useful UI evidence

- Use `testbench.snapshot_qt_state(label)` at meaningful checkpoints, especially before/after dialogs open or close and after state-changing commands. It returns plain data for top-level windows, named controls, dialogs and their buttons, actions, and tree headers/rows, including selection and expansion.
- Include the relevant `objectName`, visible/enabled state, labels, action text/shortcut/enabled state, tree columns/roles, row identifiers, dirty indicators, and dialog controls in focused assertions. Use snapshots for diagnosis and stable assertions for pass/fail.
- When evaluating the running Stageviz application, use its MCP server as the live-app control and observation path. Capture stage/session state with the relevant MCP tools, then capture a visual screenshot at the same checkpoint so the USD/session result can be compared with the actual UI presentation. Do not substitute a screenshot alone for state assertions.
- Capture screenshots through the Stageviz MCP Python entry point using Qt, for example `QApplication.primaryScreen().grabWindow(0).save(path)`, and include the visible top-level windows/dialogs in the capture. Save the image to a known path, inspect it, and attach or report that path with the test result. Prefer MCP/Qt screenshots over shell screen-capture tools when evaluating Stageviz.
- `stageviz_run_python` clears the application's undo/redo history before executing. Treat it as a mutating test action even when its script only captures a screenshot; use it deliberately, and capture after the test or after restoring the intended stage/history baseline. Prefer read-only MCP tools for inspection when a screenshot is unnecessary.
- Native OS file panels can be outside Qt's widget tree and may not be driveable through ordinary QObject lookup. When the test is about layer behavior, use the public Session/command API to create, add, save, or copy fixture layers deterministically, then assert the UI state. Claim native-dialog coverage only if it was separately automated or visually checked.
- For an end-to-end run against an already open Stageviz process, use MCP from an external controller as an independent stage/session oracle and compare its state before and after key actions. Do not make a synchronous MCP request from an embedded Python test running on Stageviz's Qt thread; it can deadlock.

## Keep logs reliable and tests safe

- Stageviz's embedded Python output may use an ASCII encoding. Keep status labels and printed diagnostics ASCII-safe, or serialize diagnostic structures with `json.dumps(data, ensure_ascii=True)`.
- Avoid intentionally authoring into a locked/non-editable USD layer just to test rejection: OpenUSD may emit secondary-thread coding/runtime errors even when the application rejects the edit. Verify permission state and the command's preflight/rejection result before an Sdf authoring attempt.
- Avoid manual pauses when the state can be read from Qt, USD, or MCP. Reserve a pause or screenshot check for behavior that is genuinely outside available automation, such as a native OS panel or a drag indicator that has no accessible state.
- Report the test boundary clearly: distinguish automated QObject checks, USD/session checks, external MCP observations, and any native UI interaction that was not automated.
