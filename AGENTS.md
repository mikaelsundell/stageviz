# Stageviz development guide

Use these conventions when adding or changing Stageviz functionality. Follow
nearby code when it establishes a more specific pattern, and keep changes
consistent with the existing architecture.

## UI and class structure

- Keep Qt Designer interfaces in `.ui` files and let `uic` generate the UI
  wrapper. Put widget tooltips in the `.ui` file so they are visible in the
  designer and stay with the control definition.
- Set icons in C++ through the Stageviz `style()` API and its `Style::IconRole`
  values. Do not hard-code resource paths or construct duplicate icon styling
  in individual dialogs.
- For substantial dialogs and tools, provide a small, clear public interface
  and put implementation details in a private PIMPL class. Use the Layer
  Editor (`sources/layers/layerdialog.*` and `layertree.*`) as the current
  example for ownership, initialization, and signal wiring.
- Keep object state in the private class's `Data` struct. Initialize the
  struct and its owned objects through the class initialization path; access
  state as `p->d.*` from the public wrapper and `d.*` inside the private class.
- Organize the private implementation declarations consistently: lifecycle
  and initialization, utility functions, slots/actions, data accessors, then
  the `Data` struct. Keep the `Data` struct near the end of the private class.

## C++ header documentation

- Write API documentation in headers as multiline Doxygen blocks, even when
  the description is one line. Put `@brief` on its own documentation line and
  leave a blank documentation line before tags such as `@param` and `@return`.
- Place documentation blocks immediately before the declaration they describe.
  Do not use compact `/** @brief ... */` comments or trailing `///<` comments.
- Keep Doxygen grouping directives such as `@{` and `@}` in multiline blocks;
  they do not need an `@brief` tag.

## USD edits, settings, and notifications

- Route user-facing USD stage and layer modifications through undoable
  commands. Add command factories and implementations in `sources/core/command.h`
  and `command.cpp`, then expose them through the Python bindings when Python
  users need the same authoring operation. Do not write USD directly from a UI
  callback when an undoable command is appropriate.
- Use `Session` and its notifications to observe stage, edit-target, dirty,
  and layer-state changes. Keep UI state derived from the current session and
  stage instead of maintaining a second, unsynchronized copy.
- For file dialogs that remember a directory between runs, use the existing
  `settings()` API. Name directory settings with a `Dir` suffix (for example,
  `layerDir`), use the current layer or stage path as the initial directory
  when available, and persist the directory selected by the user.

## Tests and live verification

- For the Qt testing patterns used by the embedded Python suite, read
  `skills/references/python-qt-testing.md` and reuse `CTest/python/testbench.py`.
- Add native regression coverage in `CTest/regression.cpp` for critical
  command, USD, and session behavior. Add GUI/offscreen coverage in
  `CTest/gui_regression.cpp` for critical widget, signal, and presentation
  behavior. Register each case in `CTest/CMakeLists.txt` and document notable
  coverage in `CTest/README.md` when it changes the suite's scope.
- For workflows exposed to the embedded Python interpreter, add or update a
  `CTest/python/test*.py` script alongside the implementation. Build stage
  data through the public Stageviz command bindings where possible, then use
  Qt's QObject tree and stable `objectName` values to check both the underlying
  state and what the UI presents. Include undo and redo checks for authored
  changes.
- Prefer observable assertions over manual checkpoints: inspect command
  results, session state, QObject properties, tree rows, icons, enabled state,
  and dirty markers. A test may pause for a user only when the result cannot be
  detected or captured reliably through the available APIs.
- Use the configured Stageviz MCP server during development when it can inspect
  or exercise the running application and that would help verify the change.
  Combine it with automated regressions; live MCP inspection does not replace
  repeatable tests.
- For end-to-end tests against a running Stageviz instance, use the MCP server
  as an independent USD/session oracle. Capture `stageviz_get_stage_info` and,
  where relevant, `stageviz_list_prims`, `stageviz_get_prim_info`, or
  `stageviz_get_selection` before and after important test actions; compare the
  results with the script's USD and Qt assertions. Drive these MCP calls from
  an external client or test controller. MCP tool requests are dispatched on
  Stageviz's Qt application thread, so a synchronous MCP request from an
  embedded Python test can deadlock while that same thread is running the test.
  Avoid `stageviz_run_python` for read-only inspection because it clears undo
  history.
- Capture reusable Qt snapshots at meaningful UI checkpoints, especially
  before and after dialogs open/close and after state-changing commands. Include
  top-level windows and dialogs, stable `objectName` values, visible/enabled
  state, dialog labels and buttons, action state, and tree headers/rows with
  expansion and selection. Store plain Python data in snapshots rather than
  retaining Qt wrappers across event processing. Native OS dialogs that are
  outside Qt's widget tree still need a GUI-level screenshot or automation
  check. Enumerate widgets with the static
  `QApplication.topLevelWidgets()` / `QApplication.allWidgets()` APIs rather
  than methods on `QApplication.instance()`; PySide may expose that instance as
  a `QCoreApplication` wrapper.
- Run the focused build and regression cases relevant to the change, then
  report exactly what was and was not verified.

## Commit messages

- Write commit messages with a short, lowercase type prefix and a concise
  summary, followed by dash bullets. Each bullet starts with a clear subject
  header, then describes that part of the change. For example:

  ```text
  fix: disable native instancing for leaf geometry prims
  - Context menu: Disable Duplicate > Instance for leaf geometry prims
  - Edit menu: Disable Duplicate > Instance for leaf geometry prims
  ```
