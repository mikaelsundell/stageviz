# SPDX-License-Identifier: BSD-3-Clause
"""Runnable, dependency-free example of a single-file conversion plugin.

Import this module from stageviz.py and call register().
An .svshape file contains JSON such as {"radius": 2.0}.
"""
import json
from pathlib import Path

from pxr import Usd, UsdGeom
import stageviz


def load(source, options):
    radius = float(json.loads(Path(source).read_text(encoding='utf-8'))['radius'])
    radius *= options.get('scale', 1.0)
    stage = Usd.Stage.CreateInMemory()
    sphere = UsdGeom.Sphere.Define(stage, '/Sphere')
    sphere.GetRadiusAttr().Set(radius)
    stage.SetDefaultPrim(sphere.GetPrim())
    UsdGeom.SetStageUpAxis(stage, UsdGeom.Tokens.z)
    UsdGeom.SetStageMetersPerUnit(stage, 1.0)
    return stage


def save(stage, destination, options):
    roots = list(stage.GetPseudoRoot().GetChildren())
    if len(roots) != 1 or not roots[0].IsA(UsdGeom.Sphere):
        raise ValueError('The example format only supports a single root Sphere')
    # Deliberately reject data this tiny example cannot represent.
    sphere = UsdGeom.Sphere(roots[0])
    if sphere.GetOrderedXformOps() or list(roots[0].GetChildren()):
        raise ValueError('The example format cannot represent transforms or children')
    Path(destination).write_text(json.dumps({'radius': sphere.GetRadiusAttr().Get()}), encoding='utf-8')


def settings(path, operation, parent):
    if operation == 'export':
        return {}
    try:
        from PySide6.QtWidgets import QInputDialog
    except ImportError:
        from PyQt6.QtWidgets import QInputDialog
    scale, accepted = QInputDialog.getDouble(parent, 'Import Simple Shape',
                                            'Radius scale', 1.0, 0.001, 1000.0, 3)
    return {'scale': scale} if accepted else None


def register():
    stageviz.plugins.registerFileFormat('Simple Shape', ['svshape'], loader=load, saver=save, options=settings)
