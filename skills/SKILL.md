---
name: stageviz-python
description: Create, modify, debug, review, and explain Python tools for Stageviz using the canonical Stageviz Python bindings, OpenUSD, PySide6/PyQt6, MaterialX, and Hydra rendering. Use for Stageviz scripts, stage editing, selection, payloads, namespace operations, attributes, materials, ViewState/ViewCamera control, Session workflows, offscreen rendering, standalone dialogs, dockable panels, inspectors, browsers, shelf tools, material/look editors, and other Python UI running inside Stageviz. Treat bundled references as authoritative and never invent Stageviz methods, arguments, enum values, convenience globals, or behavior not documented there or proven by user-supplied code.
---

# Stageviz Python

Produce ready-to-run Python for the Stageviz embedded interpreter and reason about the public Stageviz Python interface.

## Required references

Before generating or modifying Stageviz Python, load the reference that matches the task:

- For any Stageviz API use, including `Session`, `SelectionList`, `ViewState`, `ViewCamera`, `Application`, `Style`, `RenderEngine`, `stageviz.command`, stage access, edit layers, selection, payloads, namespace edits, attributes, transforms, materials, progress, or rendering, read [references/python-api.md](references/python-api.md).
- For any Qt UI work, including dialogs, tool windows, dock widgets, inspectors, browsers, material/look tools, primitive tools, shelf tools, selection utilities, or other interactive Stageviz Python UI, ALWAYS read [references/python-dialogs.md](references/python-dialogs.md) before generating code.
- For requests involving both Stageviz API calls and Qt UI, read both references before producing the script.
- For reusable Qt scaffolding and concrete helper implementations, inspect [scripts/dialogs.py](scripts/dialogs.py). Use it as a pattern or import it only when the target Stageviz environment will ship this helper module.

Do not replace these references with assumptions from the C++ API or from other DCC applications.

## Core API rules

1. Treat `references/python-api.md` as the canonical Stageviz Python surface.
2. Do not infer Python methods from similarly named C++ methods.
3. Preserve exact API casing:
   - `stageviz.command` uses snake_case.
   - `Session`, `SelectionList`, `ViewState`, `ViewCamera`, `Application`, and `Style` use the camelCase methods documented in the bindings.
   - `RenderEngine` uses snake_case.
4. Prefer `stageviz.command` for user-edit operations that should participate in Stageviz undo/redo.
5. Use `Session`, `SelectionList`, `ViewState`, and `ViewCamera` for querying state and direct runtime/view control.
6. Use normal `pxr` APIs on the stage returned by `Session.stage()` when the requested operation is not exposed by `stageviz.command`. State clearly when code is using OpenUSD directly rather than a Stageviz-specific API.
7. Never guess integer enum values. Use a numeric value only when the reference, current user-supplied code, or the user's installed API proves it.
8. Prefer `stage()` and `auxiliary()` in ordinary scripts. Treat `stageUnsafe()` and `auxiliaryUnsafe()` as advanced access that assumes the caller already owns the appropriate native lock.
9. Treat `stageLock()`, `auxiliaryLock()`, and `commandStack()` as native pointer addresses, not Python lock or command objects.
10. Do not invent convenience globals such as `stageviz.session()` or `stageviz.application()`. Preserve them only when the user's existing script proves that their installed build exposes them.
11. If the requested capability is absent from the reference, say so. Then use an appropriate `pxr` API, propose a binding addition, or ask for newer binding source.

## Recommended object access

Use these entry points unless the user's existing script establishes another valid pattern:

```python
import stageviz

session = stageviz.Session()
selection = session.selection()
view_state = session.viewState()
camera = view_state.camera()
app = stageviz.Application()
style = stageviz.Style()
```

Create `stageviz.RenderEngine()` directly for offscreen rendering. Use functions under `stageviz.command` for undoable commands.

## Editing workflow

For normal Stageviz scene editing:

1. Read the current selection with `session.paths()` or `session.selection().paths()`.
2. Validate required prim or property paths with `pxr` APIs when necessary.
3. Use the closest documented `stageviz.command` operation when one exists.
4. Use direct OpenUSD authoring only when the command surface does not expose the operation or when the user explicitly requests direct authoring.
5. Use `session.notifyRedraw()` only when the requested direct runtime change actually requires it.
6. Do not wrap `stageviz.command` operations in a second undo system; they already run through Stageviz's native command stack.

## Qt dialogs and tools

For every dialog, panel, inspector, browser, material editor, shelf tool, or dock-widget request, follow [references/python-dialogs.md](references/python-dialogs.md) in addition to the API reference.

The dialog reference defines the Stageviz conventions for:

- PySide6/PyQt6 compatibility.
- Stageviz main-window parenting.
- Non-modal standalone dialog lifecycle and retained Python references.
- `QDockWidget` construction and registration.
- Current session/stage/selection access.
- Dialog-local shortcut ownership.
- QColor handling.
- MaterialX and `UsdShade` authoring patterns.
- Material binding.
- Safe batching/deferred prim-update behavior without inventing enum symbols.
- Error reporting and traceback handling.
- Preserving working dialog scaffolding when modifying an existing script.

When a user asks for a Stageviz dialog script, return a complete runnable script unless they explicitly request only a patch.

Use `scripts/dialogs.py` as a reusable implementation reference for main-window lookup, QApplication handling, retained dialog lifetime, standalone tool windows, dock widgets, and shortcut ownership. Generated standalone scripts should remain self-contained unless the user explicitly wants to depend on this helper module.

## Output conventions

- Prefer complete, directly runnable Stageviz Python over pseudocode.
- Keep explanatory prose short unless the user asks for a detailed explanation.
- Identify assumptions not guaranteed by the bindings.
- When debugging, distinguish Stageviz binding behavior from PySide/PyQt behavior, OpenUSD behavior, and inference from user-provided code.
- When modifying an existing source file, preserve unrelated formatting, structure, and conventions as much as possible.
