# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2025 - present Mikael Sundell
# https://github.com/mikaelsundell/stageviz

from pxr import Usd, UsdGeom, Sdf, Gf
import os
import tempfile


def file_size(path):
    return os.path.getsize(path) if os.path.exists(path) else 0


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


def make_payload_layer(path, count=100):
    stage = Usd.Stage.CreateNew(path)
    root = UsdGeom.Xform.Define(stage, "/PayloadRoot").GetPrim()
    stage.SetDefaultPrim(root)

    for i in range(count):
        prim = UsdGeom.Xform.Define(stage, f"/PayloadRoot/PayloadPrim_{i:05d}").GetPrim()
        UsdGeom.XformCommonAPI(prim).SetTranslate(Gf.Vec3d(i, i * 2, i * 3))

        attr = prim.CreateAttribute("stageviz:payloadValue", Sdf.ValueTypeNames.String)
        attr.Set("payload" * 128)

    stage.GetRootLayer().Save()
    return stage


def make_heavy_stage(path, payload_path, count=2000):
    stage = Usd.Stage.CreateNew(path)

    root = UsdGeom.Xform.Define(stage, "/root").GetPrim()
    stage.SetDefaultPrim(root)

    payload_holder = UsdGeom.Xform.Define(stage, "/root/PayloadHolder").GetPrim()
    payload_holder.GetPayloads().AddPayload(
        Sdf.Payload(os.path.basename(payload_path), "/PayloadRoot")
    )

    for i in range(count):
        prim = UsdGeom.Xform.Define(stage, f"/root/Prim_{i:05d}").GetPrim()
        UsdGeom.XformCommonAPI(prim).SetTranslate(Gf.Vec3d(i, i * 2, i * 3))

        attr = prim.CreateAttribute("stageviz:testValue", Sdf.ValueTypeNames.String)
        attr.Set("x" * 256)

    stage.GetRootLayer().Save()
    return stage


def print_stage_state(label, path):
    stage = Usd.Stage.Open(path)
    root_layer = stage.GetRootLayer()

    print(label)
    print("  file:", path)
    print("  size:", file_size(path))
    print("  root layer specs:", count_specs(root_layer))
    print("  /root exists:", bool(stage.GetPrimAtPath("/root")))
    print("  /root/PayloadHolder exists:", bool(stage.GetPrimAtPath("/root/PayloadHolder")))
    print("  payload composed child exists:",
          bool(stage.GetPrimAtPath("/root/PayloadHolder/PayloadPrim_00000")))

    return stage


def run():
    with tempfile.TemporaryDirectory() as tmp:
        payload_layer = os.path.join(tmp, "payload.usdc")
        original = os.path.join(tmp, "delete_save_test.usdc")
        exported_layer = os.path.join(tmp, "delete_export_layer_test.usdc")
        flattened_stage = os.path.join(tmp, "delete_flattened_stage_test.usdc")

        print("Creating payload USDC...")
        make_payload_layer(payload_layer, count=100)

        print("Creating main USDC...")
        stage = make_heavy_stage(original, payload_layer, count=2000)
        root_layer = stage.GetRootLayer()

        print_stage_state("\nAfter create:", original)

        print("\nDeleting local heavy prims but keeping payload holder...")
        stage.SetEditTarget(root_layer)

        for i in range(2000):
            stage.RemovePrim(f"/root/Prim_{i:05d}")

        print("  /root exists in composed stage:", bool(stage.GetPrimAtPath("/root")))
        print("  /root/PayloadHolder exists:", bool(stage.GetPrimAtPath("/root/PayloadHolder")))
        print("  payload composed child exists:",
              bool(stage.GetPrimAtPath("/root/PayloadHolder/PayloadPrim_00000")))
        print("  root layer specs after delete:", count_specs(root_layer))

        print("\nSaving same file using rootLayer.Save()...")
        root_layer.Save()
        print_stage_state("After Save same file:", original)

        print("\nExporting root layer to fresh USDC using rootLayer.Export()...")
        root_layer.Export(exported_layer)
        print_stage_state("After SdfLayer.Export:", exported_layer)

        print("\nExporting stage using stage.Export() -- flattened composed stage...")
        stage.Export(flattened_stage)
        print_stage_state("After UsdStage.Export flattened:", flattened_stage)

        print("\nSummary:")
        print("  payload file:", file_size(payload_layer))
        print("  Save same file:", file_size(original))
        print("  SdfLayer.Export fresh file:", file_size(exported_layer))
        print("  UsdStage.Export flattened file:", file_size(flattened_stage))


if __name__ == "__main__":
    run()