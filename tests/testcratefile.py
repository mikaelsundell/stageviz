# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2025 - present Mikael Sundell
# https://github.com/mikaelsundell/stageviz

from pxr import Usd, UsdGeom, Sdf, Gf
import os
import tempfile
import shutil


def file_size(path):
    return os.path.getsize(path) if os.path.exists(path) else 0


def make_heavy_stage(path, count=2000):
    stage = Usd.Stage.CreateNew(path)
    root = UsdGeom.Xform.Define(stage, "/root").GetPrim()
    stage.SetDefaultPrim(root)

    for i in range(count):
        prim = UsdGeom.Xform.Define(stage, f"/root/Prim_{i:05d}").GetPrim()
        UsdGeom.XformCommonAPI(prim).SetTranslate(Gf.Vec3d(i, i * 2, i * 3))

        attr = prim.CreateAttribute("stageviz:testValue", Sdf.ValueTypeNames.String)
        attr.Set("x" * 256)

    stage.GetRootLayer().Save()
    return stage


def count_specs(layer):
    count = 0

    def walk(prim_spec):
        nonlocal count
        count += 1

        for prop in prim_spec.properties:
            count += 1

        for child in prim_spec.nameChildren:
            walk(child)

    for root in layer.rootPrims:
        walk(root)

    return count


def run():
    with tempfile.TemporaryDirectory() as tmp:
        original = os.path.join(tmp, "delete_save_test.usdc")
        exported_layer = os.path.join(tmp, "delete_export_layer_test.usdc")
        flattened_stage = os.path.join(tmp, "delete_flattened_stage_test.usdc")

        print("Creating heavy USDC...")
        stage = make_heavy_stage(original)

        root_layer = stage.GetRootLayer()

        print("\nAfter create:")
        print("  file:", original)
        print("  size:", file_size(original))
        print("  root layer specs:", count_specs(root_layer))
        print("  /root exists:", bool(stage.GetPrimAtPath("/root")))

        print("\nDeleting /root using stage.RemovePrim('/root')...")
        stage.SetEditTarget(root_layer)
        ok = stage.RemovePrim("/root")
        print("  RemovePrim returned:", ok)
        print("  /root exists in composed stage:", bool(stage.GetPrimAtPath("/root")))
        print("  root layer specs after delete:", count_specs(root_layer))

        print("\nSaving same file using rootLayer.Save()...")
        root_layer.Save()

        print("  same file size after Save:", file_size(original))

        reopened = Usd.Stage.Open(original)
        print("  reopened /root exists:", bool(reopened.GetPrimAtPath("/root")))
        print("  reopened root layer specs:", count_specs(reopened.GetRootLayer()))

        print("\nExporting root layer to fresh USDC using rootLayer.Export()...")
        root_layer.Export(exported_layer)

        exported = Usd.Stage.Open(exported_layer)
        print("  exported layer file size:", file_size(exported_layer))
        print("  exported /root exists:", bool(exported.GetPrimAtPath("/root")))
        print("  exported root layer specs:", count_specs(exported.GetRootLayer()))

        print("\nExporting stage using stage.Export() -- this is flattened...")
        stage.Export(flattened_stage)

        flattened = Usd.Stage.Open(flattened_stage)
        print("  flattened stage file size:", file_size(flattened_stage))
        print("  flattened /root exists:", bool(flattened.GetPrimAtPath("/root")))
        print("  flattened root layer specs:", count_specs(flattened.GetRootLayer()))

        print("\nSummary:")
        print("  Save same file:", file_size(original))
        print("  SdfLayer.Export fresh file:", file_size(exported_layer))
        print("  UsdStage.Export flattened file:", file_size(flattened_stage))


if __name__ == "__main__":
    run()