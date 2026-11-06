# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 - present Mikael Sundell

"""Verify owner deltas for a sweep containing shared sources and proxies.

Run only in a disposable viewer, with _SHARED_VIEWPORT_FILE identifying a saved
USD fixture. The input file is never saved. External MCP controllers can call
setup(), sweep(), drag(mode), and cleanup() separately; MCP calls must stay
outside the embedded interpreter. Loading this module does not start a test.
"""

from pathlib import Path
import contextlib
import io
import runpy

import stageviz
from pxr import Usd, UsdGeom

_base = runpy.run_path(str(Path(__file__).with_name("testviewport.py")),
                      init_globals={"_VIEWPORT_AUTORUN": False})
tb = _base["tb"]


class SharedSourceViewportTest(_base["ViewportTest"]):
    def snapshot(self, label):
        with contextlib.redirect_stdout(io.StringIO()):
            super().snapshot(label)
        print("Shared-source checkpoint:", label, "selected meshes:", len(stageviz.session().paths()))

    def setup(self, filename):
        assert self.disposable, "shared-source dataset tests require a disposable viewer"
        session = stageviz.session()
        self.original_filename = session.filename()
        self.original_selection = list(session.paths())
        assert session.loadFromFile(str(filename))
        tb.process_events(250)
        stage = session.stage()
        self.proxies = [str(prim.GetPath()) for prim in Usd.PrimRange.Stage(stage, Usd.TraverseInstanceProxies())
                        if prim.IsA(UsdGeom.Mesh)]
        assert any(stage.GetPrimAtPath(path).IsInstanceProxy() for path in self.proxies)
        sources = [path for path in self.proxies if not stage.GetPrimAtPath(path).IsInstanceProxy()]
        assert sources, "fixture contains ordinary source geometry as well as proxies"
        # This workflow exercises internal-reference sources and their instance
        # roots. Track roots as well as mesh descendants for per-object rotation.
        instances = [prim for prim in stage.Traverse() if prim.IsInstance()]
        roots = []
        for prim in instances:
            references = prim.GetMetadata('references')
            for ref in references.GetAddedOrExplicitItems() if references else []:
                if not ref.assetPath and ref.primPath and str(ref.primPath) not in roots:
                    roots.append(str(ref.primPath))
            if str(prim.GetPath()) not in roots:
                roots.append(str(prim.GetPath()))
        self.roots = roots or [sources[0]]
        stageviz.command.select_paths([])
        session.viewState().camera().setAspectRatioLocked(False)
        self.activate_mode("Select")
        tb.trigger_action("displayFrameAll")
        session.viewState().camera().distance(1.8)
        tb.process_events(300)
        self.snapshot("shared_loaded")

    def sweep(self):
        QtCore, _, QtTest, _ = tb.qt()
        self.activate_mode("Select")
        widget = self.viewport()
        QtTest.QTest.mousePress(widget, QtCore.Qt.MouseButton.LeftButton, pos=QtCore.QPoint(8, 8))
        end = QtCore.QPoint(widget.width() - 8, widget.height() - 8)
        QtTest.QTest.mouseMove(widget, end, 50)
        QtTest.QTest.mouseRelease(widget, QtCore.Qt.MouseButton.LeftButton, pos=end)
        tb.process_events(150)
        assert set(stageviz.session().paths()) == set(self.proxies), "sweep includes every source and proxy mesh"
        self.center = (widget.width() // 2, widget.height() // 2)
        self.snapshot("shared_swept")
