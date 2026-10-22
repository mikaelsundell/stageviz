# Native regression tests

This directory registers native C++ regression tests through CTest. Enable
them with `BUILD_TESTING=ON` in a configured Stageviz development build. The
native executable uses a separate `QCoreApplication`; the GUI executable uses
`QApplication` and offscreen rendering. Neither starts the full viewer or
initializes the embedded Python interpreter.

Native CTest coverage includes stage/session persistence, namespace and
composition edits, payload state, USD utilities, selection-list ordering and
large-set semantics, command undo/redo, tree selection backends, styles,
materials, geometry subsets, and RenderEngine initialization/image output.
Coverage also includes repeated ASCII and Crate saves, failure recovery, Save As
asset rebasing, namespace load rules, merge asset-path anchoring, and selected
export of session-layer opinions.

The Python suites in `python/testapi.py` and `python/testui.py` run inside the
Stageviz Python editor. They cover the embedded command API and live Outliner UI
respectively; CTest cannot launch them because they require an initialized
Stageviz application session. Run them manually from that editor after loading
the expected Stageviz application environment.

The in-app API suite covers command selection, select-all/invert, isolation,
selection lifecycle during edits, and now native instance proxy paths. The UI
suite covers Outliner selection projection and now verifies visible rows for
two proxies sharing one prototype, preserving the selected proxy/root identity
without selecting the source or sibling instance. CTest registers every case
implemented by the native and GUI executables.

Known boundary: the offscreen RenderEngine smoke test validates renderer setup
and image production, but does not validate the viewport selection outline or
mouse-driven click/sweep behavior. Those require a current on-screen GL context
and interactive viewport. Add a deterministic GL viewport harness before
treating those as covered by automated regressions.

Tests use temporary directories and do not modify project fixtures. Runtime
dependency paths must be available as for a normal development build.

On macOS, the application and test executable link `TBB::tbb` explicitly so
Debug builds inherit TBB's debug compile definitions and runtime selection.
