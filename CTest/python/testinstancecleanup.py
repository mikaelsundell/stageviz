# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 - present Mikael Sundell
"""Deleting exported instances cleans their unused flattened sources atomically."""
from pathlib import Path
import json
import runpy

import stageviz
from pxr import Usd, UsdGeom

_base = runpy.run_path(str(Path(__file__).with_name('testviewport.py')),
                      init_globals={'_VIEWPORT_AUTORUN': False})
tb = _base['tb']


class InstanceCleanupTest(_base['ViewportTest']):
    def setup(self):
        assert self.disposable
        self.original_filename = stageviz.session().filename()
        self.original_selection = list(stageviz.session().paths())
        stage = Usd.Stage.CreateInMemory()
        UsdGeom.Cube.Define(stage, '/Source/Geom')
        for name in ('One', 'Two'):
            prim = UsdGeom.Xform.Define(stage, '/' + name).GetPrim()
            prim.GetReferences().AddInternalReference('/Source')
            prim.SetInstanceable(True)
        self.fixture = self.directory / 'flattened.usda'
        stage.Flatten().Export(str(self.fixture))
        assert stageviz.session().load(str(self.fixture)) and tb.wait_for_commands()
        stage = stageviz.session().stage()
        self.helpers = [str(p.GetPath()) for p in stage.GetPseudoRoot().GetAllChildren()
                        if p.GetName().startswith('Flattened_Prototype_')]
        assert len(self.helpers) == 1
        stageviz.command.delete_paths(['/Source'])
        assert tb.wait_for_commands()
        self.check('setup', True, True)

    def check(self, label, one, two):
        stage = stageviz.session().stage()
        assert bool(stage.GetPrimAtPath('/One')) == one
        assert bool(stage.GetPrimAtPath('/Two')) == two
        assert bool(stage.GetRootLayer().GetPrimAtPath(self.helpers[0])) == (one or two)
        assert len(stage.GetPrototypes()) == int(one or two)
        tb.process_events(100)
        self.snapshot(label)
        print('INSTANCE_CLEANUP_CHECKPOINT', json.dumps({'roots': sorted(
            str(p.GetPath()) for p in stage.GetPseudoRoot().GetChildren()),
            'helper': self.helpers[0], 'helperExists': bool(one or two)}))

    def shared(self):
        stageviz.command.delete_paths(['/One'])
        assert tb.wait_for_commands()
        self.check('shared', False, True)

    def last(self):
        stageviz.command.delete_paths(['/Two'])
        assert tb.wait_for_commands()
        self.check('empty', False, False)
        for suffix in ('usda', 'usdc'):
            saved = self.directory / ('empty.' + suffix)
            stageviz.session().stage().GetRootLayer().Export(str(saved))
            reopened = Usd.Stage.Open(str(saved))
            assert not list(reopened.GetPseudoRoot().GetAllChildren())
            assert not reopened.GetPrototypes()
        stageviz.command.undo()
        assert tb.wait_for_commands()
        self.check('undo', False, True)
        stageviz.command.redo()
        assert tb.wait_for_commands()
        self.check('redo', False, False)
