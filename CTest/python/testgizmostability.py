# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 - present Mikael Sundell

"""Orbit with Rotate active in a disposable viewer, checking meshes and pixels.

Use an external MCP controller for setup(), orbit(), distances(), edge_on(),
clipping() and cleanup().
This stress case replaces the stage and must not run on unsaved user work.
"""

from pathlib import Path
import math
import runpy

import stageviz
from pxr import UsdGeom

_base = runpy.run_path(str(Path(__file__).with_name('testviewport.py')),
                      init_globals={'_VIEWPORT_AUTORUN': False})
tb = _base['tb']


class GizmoStabilityTest(_base['ViewportTest']):
    def setup(self):
        assert self.disposable, 'gizmo stress requires a disposable process'
        self.original_filename = ''
        self.original_selection = []
        assert stageviz.session().newStage()
        self.activate_mode('Rotate')
        for name in ('Plane', 'Sphere'):
            stageviz.command.new_prim('/World', name, type_name=name)
            tb.wait_for_commands()
        stageviz.command.new_scope('/', 'Materials')
        tb.wait_for_commands()
        stageviz.command.new_material('/Materials', 'Test')
        tb.wait_for_commands()
        assert stageviz.session().stage().GetPrimAtPath('/Materials/Test')
        stageviz.command.bind_material(['/World/Sphere'], '/Materials/Test')
        tb.wait_for_commands()
        assert stageviz.session().stage().GetPrimAtPath('/World/Sphere').GetRelationship('material:binding').GetTargets()
        tb.trigger_action('materialEditor')
        tb.process_events(300)
        stageviz.command.select_paths(['/World/Sphere'])
        tb.wait_for_commands()
        self.frame_handle()
        self.topology = {}
        aux = stageviz.session().auxiliary()
        for mode in ('Move', 'Rotate', 'Scale'):
            for axis in 'XYZ':
                path = '/Display/Transform/' + mode + '/' + axis
                mesh = UsdGeom.Mesh(aux.GetPrimAtPath(path))
                assert mesh.GetSubdivisionSchemeAttr().Get() == 'none'
                self.topology[path] = (mesh.GetPointsAttr().Get(), mesh.GetFaceVertexIndicesAttr().Get())
        self.check('gizmo_orbit_setup')

    def check(self, label):
        widget = self.viewport()
        widget.repaint()
        aux = stageviz.session().auxiliary()
        for path, (points, indices) in self.topology.items():
            mesh = UsdGeom.Mesh(aux.GetPrimAtPath(path))
            assert mesh.GetPointsAttr().Get() == points and mesh.GetFaceVertexIndicesAttr().Get() == indices
        matrix = UsdGeom.XformCache().GetLocalToWorldTransform(aux.GetPrimAtPath('/Display/Transform'))
        assert all(math.isfinite(matrix[r][c]) for r in range(4) for c in range(4))
        assert list(stageviz.session().paths()) == ['/World/Sphere']
        self.snapshot(label)
        widget = self.viewport()
        # Check the rendered handles as well as their USD points. A GPU-only
        # spike would leave the authored mesh unchanged. The neutral fixture
        # has no saturated red/green/blue surfaces other than the handles and
        # camera-axis indicator; exclude that indicator's lower-left corner.
        image = widget.grab().toImage().scaledToWidth(320)
        ratio = image.width() / widget.width()
        colored = []
        for y in range(image.height()):
            for x in range(image.width()):
                if x < 80 * ratio and y > image.height() - 80 * ratio:
                    continue
                c = image.pixelColor(x, y)
                r, g, b = c.red(), c.green(), c.blue()
                if ((r > 100 and r > 2.5 * g and r > 2 * b)
                        or (g > 100 and g > 1.7 * r and g > 1.7 * b)
                        or (b > 100 and b > 1.7 * r and b > 1.5 * g)):
                    colored.append((x, y))
        assert len(colored) >= 5, 'rotation handles are visible in the rendered frame'
        limit = 2.8 * stageviz.ui().gizmoSize(stageviz.UI.Medium) * ratio
        for component in (0, 1):
            span = max(p[component] for p in colored) - min(p[component] for p in colored)
            assert span < limit, ('rendered gizmo extends beyond its screen-space size', label, span, limit)

    def orbit(self):
        camera = stageviz.session().viewState().camera()
        for i in range(144):
            camera.tumble(7.5, 5.0)
            self.viewport().repaint()
            tb.process_events(5)
            if i % 24 == 23:
                self.check('gizmo_orbit_' + str(i))

    def distances(self):
        camera = stageviz.session().viewState().camera()
        for distance in (1.15, 2.5, 8.0, 35.0):
            camera.setCameraDistance(distance)
            for i in range(48):
                camera.tumble(7.5, -7.5)
                self.viewport().repaint()
                tb.process_events(5)
            self.check('gizmo_distance_' + str(distance))

    def clipping(self):
        """Handles remain visible and resize outside the document clip range."""
        session = stageviz.session()
        camera = session.viewState().camera()
        original_clipping = (camera.nearClipping(), camera.farClipping())
        before = session.stage().GetRootLayer().ExportToString()
        try:
            camera.setNearClipping(1000)
            camera.setFarClipping(1001)
            for mode in ('Move', 'Rotate', 'Scale'):
                self.activate_mode(mode)
                radii = []
                for distance in (8., 16.):
                    camera.setCameraDistance(distance)
                    self.viewport().repaint()
                    tb.process_events(20)
                    self.check('gizmo_clipping_' + mode + '_' + str(distance))
                    handle = session.auxiliary().GetPrimAtPath('/Display/Transform')
                    matrix = UsdGeom.XformCache().GetLocalToWorldTransform(handle)
                    radii.append(sum(matrix[0][i] ** 2 for i in range(3)) ** .5)
                    assert (camera.nearClipping(), camera.farClipping()) == (1000, 1001)
                assert radii[1] > radii[0] * 1.5, 'handles still resize with camera distance'
            assert session.stage().GetRootLayer().ExportToString() == before
        finally:
            camera.setNearClipping(original_clipping[0])
            camera.setFarClipping(original_clipping[1])
            self.activate_mode('Rotate')

    def edge_on(self):
        camera = stageviz.session().viewState().camera()
        camera.setAxisYaw(0)
        camera.setAxisPitch(45)
        camera.setCameraDistance(2.5)
        for sweep in range(4):
            for i in range(41):
                angle = -5 + i * .25
                camera.setAxisRoll(angle if sweep % 2 == 0 else -angle)
                self.viewport().repaint()
                tb.process_events(8)
                if i % 10 == 0:
                    self.check('gizmo_edge_on_%d_%d' % (sweep, i))
        # Exercise the actual Alt-left orbit route, not only camera setters.
        core, gui, qt_test, widgets = tb.qt()
        before = stageviz.session().stage().GetRootLayer().ExportToString()
        camera.setAxisRoll(0)
        camera.setCameraDistance(5)
        widget = self.viewport()
        start = core.QPoint(20, widget.height() // 2)
        qt_test.QTest.mousePress(widget, core.Qt.MouseButton.LeftButton,
                                core.Qt.KeyboardModifier.AltModifier, start)
        for i in range(121):
            x = round(20 * math.sin(i * math.pi / 30))
            pos = start + core.QPoint(x, 0)
            widget = self.viewport()
            event = gui.QMouseEvent(core.QEvent.Type.MouseMove, core.QPointF(pos),
                                   core.QPointF(widget.mapToGlobal(pos)), core.Qt.MouseButton.NoButton,
                                   core.Qt.MouseButton.LeftButton, core.Qt.KeyboardModifier.AltModifier)
            widgets.QApplication.sendEvent(widget, event)
            widget.repaint()
            tb.process_events(8)
            if i % 30 == 0:
                self.check('gizmo_edge_on_mouse_' + str(i))
        qt_test.QTest.mouseRelease(self.viewport(), core.Qt.MouseButton.LeftButton,
                                  core.Qt.KeyboardModifier.AltModifier, start)
        assert stageviz.session().stage().GetRootLayer().ExportToString() == before, 'camera orbit does not edit the prim'


def run(disposable=False):
    test = GizmoStabilityTest(disposable=disposable)
    try:
        test.setup()
        test.orbit()
        test.distances()
        test.edge_on()
        test.clipping()
    finally:
        test.cleanup()


if globals().get('_VIEWPORT_AUTORUN', True):
    run(disposable=globals().get('_VIEWPORT_DISPOSABLE', False))
