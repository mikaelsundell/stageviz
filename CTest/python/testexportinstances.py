# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 - present Mikael Sundell

"""Subset exports retain native instance geometry and proxy dependencies."""
import io
import json
from contextlib import redirect_stdout
from pathlib import Path
import sys
import tempfile

import stageviz
from pxr import Usd, UsdGeom, UsdShade

sys.path.insert(0, str(Path(__file__).resolve().parent))
import testbench as tb


class ExportInstancesTest:
    def __init__(self, disposable=False):
        assert disposable, 'this test replaces the stage'
        self.directory = Path(tempfile.mkdtemp(prefix='stageviz-export-instances-'))
        self.policy = stageviz.session().loadPolicy()

    def setup(self):
        self.fixture = self.directory / 'fixture.usda'
        stage = Usd.Stage.CreateNew(str(self.fixture))
        UsdGeom.Xform.Define(stage, '/Library').GetVisibilityAttr().Set(UsdGeom.Tokens.invisible)
        UsdGeom.Xform.Define(stage, '/Library/Source')
        body = UsdGeom.Cube.Define(stage, '/Library/Source/Body').GetPrim()
        material = UsdShade.Material.Define(stage, '/Looks/Material')
        UsdShade.MaterialBindingAPI.Apply(body).Bind(material)
        UsdGeom.Cube.Define(stage, '/Library/Source/Sibling')
        instance = UsdGeom.Xform.Define(stage, '/World/Instance').GetPrim()
        assert instance.GetReferences().AddInternalReference('/Library/Source')
        instance.SetInstanceable(True)
        UsdGeom.Cube.Define(stage, '/World/Ordinary')
        UsdGeom.Cube.Define(stage, '/World/Unrelated')
        stage.SetDefaultPrim(stage.GetPrimAtPath('/Library'))
        stage.GetRootLayer().rootPrimOrder = ['Library', 'World', 'Looks']
        assert stage.GetRootLayer().Save()
        del stage, body, material, instance
        assert stageviz.session().load(str(self.fixture))
        assert tb.wait_for_commands()

    def snapshot(self, label):
        with redirect_stdout(io.StringIO()):
            state = tb.snapshot_qt_state(label)
        state['selectedPaths'] = list(stageviz.session().paths())
        (self.directory / (label + '.json')).write_text(json.dumps(state, indent=2))
        assert tb.viewer().grab().save(str(self.directory / (label + '.png')))

    def selected(self):
        for label, path in [('instance', '/World/Instance'), ('proxy', '/World/Instance/Body')]:
            assert stageviz.session().load(str(self.fixture))
            assert tb.wait_for_commands()
            paths = ['/World/Ordinary', path]
            stageviz.command.select_paths(paths)
            assert tb.wait_for_commands()
            assert list(stageviz.session().paths()) == paths
            original = stageviz.session().stage().GetRootLayer().ExportToString()
            output = self.directory / (label + '.usda')
            assert stageviz.session().flattenPathsToFile(stageviz.session().paths(), str(output))
            assert Path(stageviz.session().filename()) == self.fixture
            assert stageviz.session().stage().GetRootLayer().ExportToString() == original
            stage = Usd.Stage.Open(str(output))
            assert not stage.GetRootLayer().HasDefaultPrim()
            assert list(stage.GetRootLayer().rootPrimOrder) == ['World', 'Looks']
            assert stage.GetPrimAtPath('/World/Instance').IsInstance()
            body = stage.GetPrimAtPath('/World/Instance/Body')
            assert body.IsInstanceProxy() and body.IsA(UsdGeom.Cube)
            assert UsdShade.MaterialBindingAPI(body).ComputeBoundMaterial()[0]
            assert stage.GetPrimAtPath('/World/Ordinary')
            assert not stage.GetPrimAtPath('/Library')
            assert not stage.GetPrimAtPath('/World/Unrelated')
            assert bool(stage.GetPrimAtPath('/World/Instance/Sibling')) == (label == 'instance')
            del stage, body
            self.snapshot(label + '_selected')
            assert stageviz.session().load(str(output))
            assert tb.wait_for_commands()
            self.snapshot(label + '_reopened')
        print('EXPORT_INSTANCES_OUTPUT', str(output))

    def cleanup(self):
        assert tb.wait_for_commands()
        assert stageviz.session().newStage(policy=self.policy)
        stageviz.command.clear()
