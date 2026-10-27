# Embedded Python test scripts

Scripts in this directory run inside Stageviz's embedded Python editor. Shared
Qt and OpenUSD utilities live in [`testbench.py`](testbench.py); workflow
coverage belongs in focused scripts such as [`testlayers.py`](testlayers.py)
and future `testmaterials.py`.

Run [`test.py`](test.py) from the Stageviz Python editor to execute the current
suite scripts in order: command/session API, Outliner/editor UI, then layer
editor workflows. It runs each script in a fresh namespace, records returned
pass/fail results, and continues after a suite failure so the final summary
shows every suite's result. Layer workflow cases may still pause for native
file dialogs or drag-and-drop checks that are not reliably driven through Qt.

## MCP and Qt snapshots

For end-to-end verification, use the running Stageviz MCP server from an
external test controller as an independent view of the active USD stage. Query
`stageviz_get_stage_info` and the relevant `stageviz_list_prims`,
`stageviz_get_prim_info`, or `stageviz_get_selection` results before and after
important authoring steps, then compare them with the Python test's USD and Qt
assertions. The endpoint defaults to `http://127.0.0.1:37651/mcp`; use the URL
reported by Stageviz if it selected another port.

Do not synchronously call the MCP endpoint from a script running in Stageviz's
embedded Python editor. MCP requests are dispatched on the Qt application
thread, so a request made while that thread is executing a test can deadlock.
Keep MCP calls in the external controller, and use its read-only tools for
inspection; `stageviz_run_python` clears native undo history.

Use `testbench.snapshot_qt_state(label)` for checkpoints involving the
presentation. It returns and prints plain data for top-level windows, named
widgets, Qt dialogs and their controls, actions, and tree headers/rows,
including item expansion and selection. Take snapshots before and after
dialogs open or close and after important commands. Reacquire Qt wrappers after
event processing rather than retaining widget objects in snapshot data. Native
OS panels may sit outside Qt's widget tree and still require a GUI screenshot
or automation check.
Use `QApplication.topLevelWidgets()` and `QApplication.allWidgets()` as static
class APIs for discovery; PySide can return a `QCoreApplication` wrapper from
`QApplication.instance()`, which does not expose those widget-enumeration
methods.

## Importing the bench

Stageviz may run a script without setting `__file__` or adding this directory
to `sys.path`. Copy the small `_import_testbench()` bootstrap from
`testlayers.py` into a new script, then import the helpers:

```python
_tb = _import_testbench()
qt = _tb.qt
process_events = _tb.process_events
require = _tb.require
session = _tb.session
command = _tb.command
stage = _tb.stage
```

Reloading the module when running a script again keeps edits to `testbench.py`
available in the long-lived embedded interpreter.

## Available helpers

- Qt discovery and lifecycle: `qt`, `viewer`, `find_widget`, `widget_visible`,
  `process_events`, `release_qt_wrappers`, and `wait_until`.
- UI diagnostics: `snapshot_qt_state` captures windows, Qt dialogs, named
  controls, actions, and tree contents as plain data for logs and comparisons.
- Qt actions and controls: `action_info`, `trigger_action`, `button_enabled`,
  `click_button`, `status_message`, and `progress_state`.
- Test flow: `require` for assertions and `pause` for checkpoints that truly
  need user interaction.
- Stageviz USD access: `session`, `command`, `stage`, `layer_stack`,
  `dirty_layers`, `layer_is_dirty`, `layer_property`, and `open_layer`.
- Disposable USD fixtures: `create_layer_file`, `create_sublayered_stage`,
  `find_layer`, and `normalize_path`.

Prefer Stageviz command bindings for changes to the live stage. Direct USD
authoring is appropriate when preparing isolated fixture files before loading
them. Assert both USD/session state and the corresponding Qt QObject
presentation. Reacquire widgets after events that rebuild dialogs or stages;
do not keep Qt wrappers across those transitions.

## Example structure

```python
_tb = _import_testbench()
qt = _tb.qt
require = _tb.require
wait_until = _tb.wait_until

session = _tb.session()
command = _tb.command()
stage = _tb.stage()
require(stage is not None, "a stage is loaded")
```

`testapi.py` covers command and session behavior; `testui.py` covers Outliner
and editor UI behavior; and `testlayers.py` covers layer authoring and
presentation. `testlayers.py` shows a full Qt + USD workflow, including fixture creation,
command bindings, dirty state, and UI assertions. A materials script should
reuse the same module and add material-specific assertions rather than
reimplementing Qt discovery, waiting, and fixture boilerplate.
