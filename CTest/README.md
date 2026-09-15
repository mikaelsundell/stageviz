# Native regression tests

This directory registers C++ regression tests through CTest. Enable them with
the CMake option `BUILD_TESTING=ON` in a configured Stageviz development build.
They use the same Qt and USD dependencies as the application and a separate
`QCoreApplication` entry point. They do not start the viewer or initialize the
embedded Python interpreter. The existing Python suites are not registered.

Coverage includes repeated ASCII and Crate saves, anonymous-root retargeting,
preservation and recovery on failed file replacement, saving edited sublayers,
rebasing Save As assets, malformed session files, current and legacy payload state,
and relationship/connection repair through moves and reverse moves. Additional
checks cover saved-layer dirty state, exact namespace load-rule preservation for
single moves, renames and batch moves, merge asset-path anchoring, and selected
export of session-layer opinions.

Tests use temporary directories and do not modify project fixtures. Runtime
dependency paths must be available as for a normal development build.

On macOS, the application and test executable link `TBB::tbb` explicitly so
Debug builds inherit TBB's debug compile definitions and runtime selection.
