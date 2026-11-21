# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 - present Mikael Sundell
"""Edited scene bounds follow front-view tumbling without reframing the camera."""
from pathlib import Path
import json
import runpy

import stageviz
from pxr import Gf, Usd, UsdGeom

_base = runpy.run_path(str(Path(__file__).with_name('testviewport.py')),
                      init_globals={'_VIEWPORT_AUTORUN': False})
tb = _base['tb']


class CameraClippingTest(_base['ViewportTest']):
    paths = ['/World/Left', '/World/Right']

    def setup(self):
        assert self.disposable
        self.original_filename = stageviz.session().filename()
        self.original_selection = list(stageviz.session().paths())
        self.fixture = self.directory / 'clipping.usda'
        stage = Usd.Stage.CreateNew(str(self.fixture))
        UsdGeom.SetStageUpAxis(stage, UsdGeom.Tokens.z)
        for path,x in zip(self.paths,(-20,20)):
            sphere = UsdGeom.Sphere.Define(stage,path)
            sphere.GetRadiusAttr().Set(3)
            sphere.AddTranslateOp().Set(Gf.Vec3d(x,0,0))
        stage.GetRootLayer().Save()
        del sphere, stage
        assert stageviz.session().load(str(self.fixture)) and tb.wait_for_commands()
        camera = stageviz.session().viewState().camera()
        camera.setBoundingBox(stageviz.session().boundingBox())
        camera.setFov(40)
        camera.setAspectRatio(1.5)
        camera.setAspectRatioLocked(True)
        camera.setCameraDistance(350)
        camera.setCameraView(stageviz.CameraViewFront)
        self.frame_bounds = camera.boundingBox()
        self.focus = camera.focusPoint()
        self.distance = camera.cameraDistance()
        self.check('initial-front')

    def check(self,label):
        tb.process_events(100)
        capture = stageviz.render().capture()
        actual = set(capture.paths())
        assert actual == set(self.paths), (label,actual)
        self.snapshot(label)
        print('CLIPPING_CHECKPOINT',json.dumps({'paths':sorted(actual), 'focus':stageviz.session().viewState().camera().focusPoint()}))

    def move_and_tumble(self):
        camera = stageviz.session().viewState().camera()
        def matrix(x):
            return tuple(tuple(row) for row in Gf.Matrix4d(1).SetTranslate(Gf.Vec3d(x,0,0)))
        stageviz.command.set_transforms(['/World/Right'],[matrix(20)],[matrix(-100)])
        assert tb.wait_for_commands()
        assert camera.boundingBox()==self.frame_bounds
        assert camera.focusPoint()==self.focus and camera.cameraDistance()==self.distance
        assert stageviz.session().boundingBox()[0][0] <= -103
        self.check('expanded-front')
        for index,delta in enumerate((15,15,15,15,-30,-30)):
            camera.tumble(delta,0)
            self.check('tumble-'+str(index))
        stageviz.command.undo()
        assert tb.wait_for_commands()
        self.check('move-undone')
        stageviz.command.redo()
        assert tb.wait_for_commands()
        self.check('move-redone')
        assert camera.focusPoint()==self.focus and camera.cameraDistance()==self.distance
