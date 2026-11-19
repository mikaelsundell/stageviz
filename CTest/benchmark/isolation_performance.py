# SPDX-License-Identifier: BSD-3-Clause
"""Synthetic isolation timings; run only through an external disposable controller."""
import contextlib
import hashlib
import io
import json
import math
from pathlib import Path
import statistics
import sys
import time

import stageviz
try:
    from PySide6 import QtOpenGLWidgets
except ImportError:
    from PyQt6 import QtOpenGLWidgets
from pxr import Gf, Sdf, Usd, UsdGeom, UsdShade, UsdLux

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'python'))
import testbench as tb


class IsolationPerformance:
    def __init__(self, output, kind, count, fixture=None):
        assert kind in ('ordinary', 'instances') and count >= 200
        self.output = Path(output)
        self.kind, self.count = kind, count
        self.fixture_path = Path(fixture) if fixture else None
        self.session = stageviz.session()
        self.rows = []

    def fixture(self):
        if self.fixture_path:
            assert self.fixture_path.is_file()
            return self.fixture_path
        path = self.output / 'fixture.usda'
        stage = Usd.Stage.CreateNew(str(path))
        UsdGeom.SetStageUpAxis(stage, UsdGeom.Tokens.y)
        UsdGeom.Xform.Define(stage, '/World')
        stage.SetDefaultPrim(stage.GetPrimAtPath('/World'))
        material = UsdShade.Material.Define(stage, '/Looks/Surface')
        shader = UsdShade.Shader.Define(stage, '/Looks/Surface/Shader')
        shader.CreateIdAttr('UsdPreviewSurface')
        shader.CreateInput('diffuseColor', Sdf.ValueTypeNames.Color3f).Set(Gf.Vec3f(.35, .4, .45))
        material.CreateSurfaceOutput().ConnectToSource(shader.CreateOutput('surface', Sdf.ValueTypeNames.Token))
        UsdLux.DistantLight.Define(stage, '/Lighting/Key')

        def mesh(path):
            shape = UsdGeom.Mesh.Define(stage, path)
            shape.GetPointsAttr().Set([(-.4, 0, -.4), (-.4, 0, .4), (.4, 0, .4), (.4, 0, -.4)])
            shape.GetExtentAttr().Set([(-.4, 0, -.4), (.4, 0, .4)])
            shape.GetFaceVertexCountsAttr().Set([4])
            shape.GetFaceVertexIndicesAttr().Set([0, 1, 2, 3])
            shape.GetSubdivisionSchemeAttr().Set('none')
            shape.GetDoubleSidedAttr().Set(True)
            UsdShade.MaterialBindingAPI.Apply(shape.GetPrim()).Bind(material)

        if self.kind == 'instances':
            UsdGeom.Xform.Define(stage, '/Library').GetVisibilityAttr().Set('invisible')
            UsdGeom.Xform.Define(stage, '/Library/Source')
            mesh('/Library/Source/Body')
        width = math.ceil(math.sqrt(self.count))
        for index in range(self.count):
            group = '/World/G%03d' % (index // 100)
            if index % 100 == 0:
                UsdGeom.Xform.Define(stage, group).MakeMatrixXform().Set(Gf.Matrix4d(1))
            root = '%s/O%05d' % (group, index)
            prim = UsdGeom.Xform.Define(stage, root)
            prim.AddTranslateOp().Set(Gf.Vec3d(index % width, 0, index // width))
            if self.kind == 'instances':
                prim.GetPrim().GetReferences().AddInternalReference('/Library/Source')
                prim.GetPrim().SetInstanceable(True)
            else:
                mesh(root + '/Body')
        stage.GetRootLayer().Save()
        return path

    def setup(self):
        fixture = self.fixture()
        assert self.session.load(str(fixture)) and tb.wait_for_commands(timeout=180)
        state = self.session.viewState()
        state.setGridEnabled(False)
        state.setRenderMode(stageviz.RenderShaded)
        state.setSceneStatsEnabled(False)
        viewer = tb.viewer()
        viewer.resize(1250, 900)
        tb.process_events(100)
        state.camera().setCameraView(stageviz.CameraViewTop)
        state.camera().frame(self.session.boundingBox(['/World']))
        state.camera().distance(1.2)
        tb.process_events(100)
        QtCore, _, _, QtWidgets = tb.qt()
        tree = next(w for w in tb.all_widgets() if w.objectName() == 'outlinerTree' and w.isVisible())
        tree.expandToDepth(1)
        iterator = QtWidgets.QTreeWidgetItemIterator(tree)
        branch = None
        while iterator.value():
            if iterator.value().data(0, QtCore.Qt.ItemDataRole.UserRole + 1) == '/World/G%03d' % ((self.count // 2) // 100):
                branch = iterator.value()
                break
            iterator += 1
        assert branch is not None
        tree.expandItem(branch)
        assert branch.childCount() == 100 and branch.isExpanded()
        stageviz.command.select_paths([])
        assert tb.wait_for_commands()
        self.original = self.session.stage().GetRootLayer().ExportToString()
        self.original_population = self.session.stage().GetPopulationMask()
        for _ in range(3):
            frame = self.image()
        self.full_coverage = self.coverage(frame)
        assert self.full_coverage > self.count * 2
        self.save_checkpoint('unisolated', frame, None)
        self.steady('full_before')

    def root(self, index):
        return '/World/G%03d/O%05d' % (index // 100, index)

    def image(self):
        viewport = next(w for w in tb.all_widgets() if w.objectName() == 'imagingGLWidget' and isinstance(w, QtOpenGLWidgets.QOpenGLWidget) and w.isVisible())
        viewport.repaint()
        image = viewport.grabFramebuffer()
        assert not image.isNull()
        return image

    def coverage(self, image):
        rgba = image.convertToFormat(image.Format.Format_RGBA8888)
        data = bytes(rgba.constBits())
        threshold = max(data[0:3]) + 12
        return sum(max(r, g, b) > threshold for r, g, b in zip(data[0::4], data[1::4], data[2::4]))

    def save_checkpoint(self, label, frame, visible_count):
        assert frame.save(str(self.output / (label + '-frame.png')))
        assert tb.viewer().grab().save(str(self.output / (label + '.png')))
        with contextlib.redirect_stdout(io.StringIO()):
            snapshot = tb.snapshot_qt_state(label)
        snapshot['mask'] = list(self.session.mask())
        snapshot['visibleCount'] = visible_count
        snapshot['foregroundPixels'] = self.coverage(frame)
        (self.output / (label + '.json')).write_text(json.dumps(snapshot, indent=2))

    def measure(self, label, mask):
        start = time.perf_counter()
        stageviz.command.isolate_paths(mask)
        dispatch = time.perf_counter()
        assert tb.wait_for_commands(timeout=180)
        completed = time.perf_counter()
        frame = self.image()
        done = time.perf_counter()
        # Object-ID readback is a correctness oracle, outside normal-frame
        # timing. Avoid its scene-wide native-instance path expansion when the
        # complete scene is restored; compare framebuffer coverage instead.
        visible_count = None
        if mask:
            visible = stageviz.render().capture().paths()
            expected = {str(prim.GetPath()) for root in mask
                        for prim in Usd.PrimRange(self.session.stage().GetPrimAtPath(root), Usd.TraverseInstanceProxies())
                        if prim.IsA(UsdGeom.Mesh)}
            assert expected and set(visible) == expected, (label, len(visible), len(expected), visible[:8])
            visible_count = len(visible)
        else:
            assert abs(self.coverage(frame) - self.full_coverage) <= self.full_coverage * .02, label
        assert list(self.session.mask()) == mask
        assert self.session.stage().GetPopulationMask() == self.original_population
        assert self.session.stage().GetRootLayer().ExportToString() == self.original
        self.rows.append(dict(label=label, dispatch_ms=(dispatch-start)*1000,
                              command_ms=(completed-start)*1000, capture_ms=(done-completed)*1000,
                              total_ms=(done-start)*1000, visible_count=visible_count))
        if label in ('first_isolate', 'parent', 'clear'):
            self.save_checkpoint(label, frame, visible_count)
        self.write()

    def steady(self, label):
        samples = []
        for _ in range(6):
            start = time.perf_counter()
            self.image()
            samples.append((time.perf_counter()-start)*1000)
        self.rows.append(dict(label=label, samples_ms=samples, total_ms=statistics.median(samples)))
        self.write()

    def parent_moves(self, parent):
        stage = self.session.stage()
        for step in range(3):
            prim = stage.GetPrimAtPath(parent)
            before = UsdGeom.XformCache().GetLocalToWorldTransform(prim)
            after = Gf.Matrix4d(before)
            after.SetTranslateOnly(Gf.Vec3d(.15 if step % 2 == 0 else -.15, 0, 0))
            start = time.perf_counter()
            stageviz.command.set_transforms([parent], [before], [after])
            dispatch = time.perf_counter()
            assert tb.wait_for_commands(timeout=180)
            completed = time.perf_counter()
            self.image()
            done = time.perf_counter()
            assert Gf.IsClose(UsdGeom.XformCache().GetLocalToWorldTransform(prim), after, 1e-8)
            self.rows.append(dict(label='parent_move', dispatch_ms=(dispatch-start)*1000,
                                  command_ms=(completed-start)*1000, capture_ms=(done-completed)*1000,
                                  total_ms=(done-start)*1000))
        for _ in range(3):
            stageviz.command.undo()
            assert tb.wait_for_commands()
        assert stage.GetRootLayer().ExportToString() == self.original
        self.write()

    def exercise(self):
        center = self.count // 2 + 3
        self.measure('first_isolate', [self.root(center)])
        self.steady('isolated_leaf')
        self.measure('same_mask', [self.root(center)])
        for i in range(8):
            self.measure('leaf_switch', [self.root(center + 1 + i % 2)])
        parent = '/World/G%03d' % (center // 100)
        self.measure('parent', [parent])
        self.steady('isolated_parent')
        self.parent_moves(parent)
        for i in range(6):
            self.measure('group_switch', ['/World/G%03d' % ((center // 100 + i + 1) % (self.count // 100))])
        self.measure('multi', [self.root(center), self.root(center + 1), self.root(center + 2)])
        self.measure('clear', [])
        self.steady('full_after')
        self.measure('reenter', [self.root(center)])
        self.measure('clear_again', [])

    def write(self):
        summary = {}
        for label in sorted({r['label'] for r in self.rows}):
            values = [r['total_ms'] for r in self.rows if r['label'] == label]
            summary[label] = dict(median_ms=statistics.median(values), max_ms=max(values), samples=len(values))
        result = dict(kind=self.kind, count=self.count, fixture_sha256=hashlib.sha256(self.original.encode()).hexdigest(),
                      rows=self.rows, summary=summary)
        (self.output / 'timings.json').write_text(json.dumps(result, indent=2))
        print('ISOLATION_TIMINGS', json.dumps(summary))

    def cleanup(self):
        assert self.session.newStage() and tb.wait_for_commands()
        stageviz.command.clear()
