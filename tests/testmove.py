from pxr import Usd, UsdGeom, Sdf
import stageviz
import tempfile
import os
import uuid
import time


# ============================================================================
# Configuration
# ============================================================================

TOTAL_XFORMS = 25000
PAYLOAD_COUNT = 5000

ASSEMBLY_COUNT = 100
GROUPS_PER_ASSEMBLY = 50

LOAD_STRIDE = 4

assert ASSEMBLY_COUNT * GROUPS_PER_ASSEMBLY == PAYLOAD_COUNT


# ============================================================================
# Helpers
# ============================================================================

def ms(seconds):
    return seconds * 1000.0


def require(condition, message):
    if not condition:
        raise RuntimeError(message)

    print("[PASS]", message)


def wait_until(predicate, timeout=30.0, interval=0.01):
    started = time.perf_counter()

    while (
        time.perf_counter()
        - started
    ) < timeout:
        if predicate():
            return True

        try:
            stageviz.process_events()
        except Exception:
            pass

        time.sleep(interval)

    return False


def stage():
    return stageviz.session().stage()


def exists(path):
    usd_stage = stage()

    if not usd_stage:
        return False

    return bool(
        usd_stage.GetPrimAtPath(
            Sdf.Path(path)
        )
    )


def loaded(path):
    usd_stage = stage()

    if not usd_stage:
        return False

    prim = usd_stage.GetPrimAtPath(
        Sdf.Path(path)
    )

    return bool(
        prim
        and prim.IsLoaded()
    )


def get_load_set():
    usd_stage = stage()

    if not usd_stage:
        return set()

    return set(
        usd_stage.GetLoadSet()
    )


def print_load_summary(label):
    usd_stage = stage()

    print("")
    print(label)

    if not usd_stage:
        print("  <no stage>")
        return

    print(
        "  rules   :",
        len(
            usd_stage
            .GetLoadRules()
            .GetRules()
        ),
    )

    print(
        "  load set:",
        len(
            usd_stage.GetLoadSet()
        ),
    )


# ============================================================================
# USD fixture creation
# ============================================================================

def make_payload_file(filename):
    payload_stage = Usd.Stage.CreateNew(
        filename
    )

    root = UsdGeom.Xform.Define(
        payload_stage,
        "/PayloadRoot",
    )

    payload_stage.SetDefaultPrim(
        root.GetPrim()
    )

    UsdGeom.Xform.Define(
        payload_stage,
        "/PayloadRoot/Geometry",
    )

    cube = UsdGeom.Cube.Define(
        payload_stage,
        "/PayloadRoot/Geometry/Box",
    )

    cube.CreateSizeAttr(10.0)

    payload_stage.GetRootLayer().Save()

    return payload_stage


def make_main_stage(
    filename,
    payload_filename,
):
    usd_stage = Usd.Stage.CreateNew(
        filename
    )

    world = UsdGeom.Xform.Define(
        usd_stage,
        "/World",
    )

    usd_stage.SetDefaultPrim(
        world.GetPrim()
    )

    xform_count = 1
    payload_count = 0

    for assembly_index in range(
        ASSEMBLY_COUNT
    ):
        assembly_path = (
            f"/World/"
            f"Assembly_{assembly_index:03d}"
        )

        UsdGeom.Xform.Define(
            usd_stage,
            assembly_path,
        )

        xform_count += 1

        for group_index in range(
            GROUPS_PER_ASSEMBLY
        ):
            group_path = (
                f"{assembly_path}/"
                f"Group_{group_index:02d}"
            )

            UsdGeom.Xform.Define(
                usd_stage,
                group_path,
            )

            xform_count += 1

            payload_path = (
                f"{group_path}/Payload"
            )

            payload = UsdGeom.Xform.Define(
                usd_stage,
                payload_path,
            )

            payload.GetPrim().GetPayloads().AddPayload(
                payload_filename,
                "/PayloadRoot",
            )

            xform_count += 1
            payload_count += 1

    filler_index = 0

    while xform_count < TOTAL_XFORMS:
        assembly_index = (
            filler_index
            % ASSEMBLY_COUNT
        )

        filler_path = (
            f"/World/"
            f"Assembly_{assembly_index:03d}/"
            f"Filler_{filler_index:05d}"
        )

        UsdGeom.Xform.Define(
            usd_stage,
            filler_path,
        )

        xform_count += 1
        filler_index += 1

    require(
        xform_count == TOTAL_XFORMS,
        f"fixture contains exactly {TOTAL_XFORMS:,} Xforms",
    )

    require(
        payload_count == PAYLOAD_COUNT,
        f"fixture contains exactly {PAYLOAD_COUNT:,} payload arcs",
    )

    usd_stage.GetRootLayer().Save()

    return usd_stage


# ============================================================================
# StageViz setup
# ============================================================================

run_id = uuid.uuid4().hex[:8]

temp_dir = os.path.join(
    tempfile.gettempdir(),
    "stageviz_25k_command_namespace_test",
)

os.makedirs(
    temp_dir,
    exist_ok=True,
)

payload_filename = os.path.join(
    temp_dir,
    f"payload_{run_id}.usda",
)

main_filename = os.path.join(
    temp_dir,
    f"main_{run_id}.usda",
)


print("")
print("=" * 78)
print("CREATE 25K STAGEVIZ COMMAND TEST")
print("=" * 78)
print("")

started = time.perf_counter()

payload_stage = make_payload_file(
    payload_filename
)

main_stage = make_main_stage(
    main_filename,
    payload_filename,
)

fixture_seconds = (
    time.perf_counter()
    - started
)

print(
    "Fixture creation:",
    f"{ms(fixture_seconds):.2f} ms",
)

payload_stage = None
main_stage = None


# ============================================================================
# Load fixture through StageViz session
# ============================================================================

session = stageviz.session()

require(
    session is not None,
    "StageViz session available",
)

started = time.perf_counter()

require(
    session.load(
        main_filename,
        stageviz.LoadNone,
    ),
    "StageViz loads fixture with LoadNone",
)

require(
    wait_until(
        lambda: (
            stage() is not None
            and exists("/World/Assembly_099")
        ),
        timeout=30.0,
    ),
    "StageViz stage becomes available",
)

load_seconds = (
    time.perf_counter()
    - started
)

print(
    "StageViz session.load:",
    f"{ms(load_seconds):.2f} ms",
)

print_load_summary(
    "Initial StageViz working set:",
)


# ============================================================================
# Explicitly load sparse working set through StageViz command interface
# ============================================================================

payloads_to_load = []

for assembly_index in range(
    ASSEMBLY_COUNT
):
    for group_index in range(
        GROUPS_PER_ASSEMBLY
    ):
        if (
            group_index
            % LOAD_STRIDE
        ) != 0:
            continue

        payloads_to_load.append(
            (
                f"/World/"
                f"Assembly_{assembly_index:03d}/"
                f"Group_{group_index:02d}/"
                f"Payload"
            )
        )

print("")
print(
    "Loading payloads through command interface:",
    len(payloads_to_load),
)

started = time.perf_counter()

stageviz.command.load_payloads(
    payloads_to_load,
)

require(
    wait_until(
        lambda: (
            loaded(
                "/World/Assembly_000/"
                "Group_00/Payload"
            )
            and loaded(
                "/World/Assembly_099/"
                "Group_48/Payload"
            )
        ),
        timeout=60.0,
    ),
    "explicit StageViz payload loads complete",
)

load_payload_seconds = (
    time.perf_counter()
    - started
)

print(
    "StageViz load_payloads:",
    f"{ms(load_payload_seconds):.2f} ms",
)

print_load_summary(
    "After StageViz explicit loads:",
)


# ============================================================================
# Baseline working set
# ============================================================================

baseline_load_set = get_load_set()

require(
    len(baseline_load_set) > 0,
    "baseline load set is populated",
)

require(
    Sdf.Path(
        "/World/Assembly_000/"
        "Group_00/Payload"
    ) in baseline_load_set,
    "move payload 1 is loaded before move",
)

require(
    Sdf.Path(
        "/World/Assembly_000/"
        "Group_04/Payload"
    ) in baseline_load_set,
    "move payload 2 is loaded before move",
)


# ============================================================================
# Rename test through StageViz command
# ============================================================================

rename_old = Sdf.Path(
    "/World/Assembly_050/Group_08"
)

rename_new = Sdf.Path(
    "/World/Assembly_050/Group_08_Renamed"
)

rename_old_payload = (
    rename_old.AppendChild("Payload")
)

rename_new_payload = (
    rename_new.AppendChild("Payload")
)

require(
    loaded(rename_old_payload.pathString),
    "rename payload is loaded before rename",
)

print("")
print("=" * 78)
print("COMMAND RENAME TEST")
print("=" * 78)
print("")

started = time.perf_counter()

stageviz.command.rename_path(
    rename_old.pathString,
    "Group_08_Renamed",
)

require(
    wait_until(
        lambda: (
            not exists(
                rename_old.pathString
            )
            and exists(
                rename_new.pathString
            )
            and loaded(
                rename_new_payload.pathString
            )
        ),
        timeout=60.0,
    ),
    "rename completes and moved payload remains loaded",
)

rename_seconds = (
    time.perf_counter()
    - started
)

print(
    "StageViz rename_path stable:",
    f"{ms(rename_seconds):.2f} ms",
)

require(
    rename_old_payload
    not in get_load_set(),
    "old renamed payload path is absent from load set",
)

require(
    rename_new_payload
    in get_load_set(),
    "new renamed payload path is present in load set",
)


# ============================================================================
# Multi-move test through StageViz command
# ============================================================================
#
# Create the destination parent THROUGH STAGEVIZ after the fixture has loaded.
# This guarantees that the destination exists in the active StageViz edit layer
# and exercises the same authoring path used by the application.
# ============================================================================

move_destination = Sdf.Path(
    "/World/MoveDestination"
)

print("")
print("Creating move destination through StageViz command...")

started = time.perf_counter()

stageviz.command.new_xform(
    "/World",
    "MoveDestination",
)

require(
    wait_until(
        lambda: exists(
            move_destination.pathString
        ),
        timeout=30.0,
    ),
    "move destination created through StageViz command",
)

destination_create_seconds = (
    time.perf_counter()
    - started
)

print(
    "Move destination creation:",
    f"{ms(destination_create_seconds):.2f} ms",
)

# new_xform selects the new destination. Clear that selection so the move
# benchmark starts from a predictable state.
stageviz.command.select_paths([])

require(
    wait_until(
        lambda: len(
            session.paths()
        ) == 0,
        timeout=5.0,
    ),
    "selection cleared before multi-move",
)


move_old_1 = Sdf.Path(
    "/World/Assembly_000/Group_00"
)

move_old_2 = Sdf.Path(
    "/World/Assembly_000/Group_04"
)

move_old_unloaded_1 = Sdf.Path(
    "/World/Assembly_000/Group_01"
)

move_old_unloaded_2 = Sdf.Path(
    "/World/Assembly_000/Group_02"
)

move_parent = move_destination

move_new_1 = Sdf.Path(
    "/World/MoveDestination/Group_00"
)

move_new_2 = Sdf.Path(
    "/World/MoveDestination/Group_04"
)

move_new_unloaded_1 = Sdf.Path(
    "/World/MoveDestination/Group_01"
)

move_new_unloaded_2 = Sdf.Path(
    "/World/MoveDestination/Group_02"
)

move_old_payload_1 = (
    move_old_1.AppendChild("Payload")
)

move_old_payload_2 = (
    move_old_2.AppendChild("Payload")
)

move_old_unloaded_payload_1 = (
    move_old_unloaded_1.AppendChild("Payload")
)

move_old_unloaded_payload_2 = (
    move_old_unloaded_2.AppendChild("Payload")
)

move_new_payload_1 = (
    move_new_1.AppendChild("Payload")
)

move_new_payload_2 = (
    move_new_2.AppendChild("Payload")
)

move_new_unloaded_payload_1 = (
    move_new_unloaded_1.AppendChild("Payload")
)

move_new_unloaded_payload_2 = (
    move_new_unloaded_2.AppendChild("Payload")
)

require(
    loaded(
        move_old_payload_1.pathString
    ),
    "move payload 1 loaded before move",
)

require(
    loaded(
        move_old_payload_2.pathString
    ),
    "move payload 2 loaded before move",
)

require(
    not loaded(
        move_old_unloaded_payload_1.pathString
    ),
    "move payload 3 unloaded before move",
)

require(
    not loaded(
        move_old_unloaded_payload_2.pathString
    ),
    "move payload 4 unloaded before move",
)

before_move_load_set = get_load_set()

unrelated_path = Sdf.Path(
    "/World/Assembly_010/"
    "Group_08/Payload"
)

require(
    unrelated_path in before_move_load_set,
    "unrelated reference payload loaded before move",
)

print("")
print("=" * 78)
print("COMMAND MULTI-MOVE TEST")
print("=" * 78)
print("")

started = time.perf_counter()

stageviz.command.move_path(
    [
        move_old_1.pathString,
        move_old_2.pathString,
        move_old_unloaded_1.pathString,
        move_old_unloaded_2.pathString,
    ],
    move_parent.pathString,
    insert_index=0,
    preserve_world_transform=True,
)

require(
    wait_until(
        lambda: (
            not exists(
                move_old_1.pathString
            )
            and not exists(
                move_old_2.pathString
            )
            and exists(
                move_new_1.pathString
            )
            and exists(
                move_new_2.pathString
            )
            and loaded(
                move_new_payload_1.pathString
            )
            and loaded(
                move_new_payload_2.pathString
            )
            and exists(
                move_new_unloaded_1.pathString
            )
            and exists(
                move_new_unloaded_2.pathString
            )
        ),
        timeout=120.0,
    ),
    "multi-move completes and moved hierarchies reach destination",
)

move_seconds = (
    time.perf_counter()
    - started
)

print(
    "StageViz move_path stable:",
    f"{ms(move_seconds):.2f} ms",
)


# ============================================================================
# Working-set correctness after move
# ============================================================================

after_move_load_set = get_load_set()

require(
    move_old_payload_1
    not in after_move_load_set,
    "old move payload 1 path removed from load set",
)

require(
    move_old_payload_2
    not in after_move_load_set,
    "old move payload 2 path removed from load set",
)

require(
    move_new_payload_1
    in after_move_load_set,
    "new move payload 1 path present in load set",
)

require(
    move_new_payload_2
    in after_move_load_set,
    "new move payload 2 path present in load set",
)

require(
    move_old_unloaded_payload_1
    not in after_move_load_set,
    "old unloaded move payload 3 path absent from load set",
)

require(
    move_old_unloaded_payload_2
    not in after_move_load_set,
    "old unloaded move payload 4 path absent from load set",
)

require(
    move_new_unloaded_payload_1
    not in after_move_load_set,
    "new move payload 3 remains absent from load set",
)

require(
    move_new_unloaded_payload_2
    not in after_move_load_set,
    "new move payload 4 remains absent from load set",
)

require(
    not loaded(
        move_new_unloaded_payload_1.pathString
    ),
    "unloaded moved payload 3 remains unloaded",
)

require(
    not loaded(
        move_new_unloaded_payload_2.pathString
    ),
    "unloaded moved payload 4 remains unloaded",
)

require(
    unrelated_path in after_move_load_set,
    "unrelated loaded payload remains loaded",
)


# ============================================================================
# Summary
# ============================================================================

print("")
print("")
print("=" * 78)
print("25K STAGEVIZ COMMAND TEST SUMMARY")
print("=" * 78)
print("")

print(
    "Fixture Xforms:",
    f"{TOTAL_XFORMS:,}",
)

print(
    "Payload arcs:",
    f"{PAYLOAD_COUNT:,}",
)

print(
    "Explicit payload loads:",
    f"{len(payloads_to_load):,}",
)

print(
    "Fixture creation:",
    f"{ms(fixture_seconds):10.2f} ms",
)

print(
    "Session load:",
    f"{ms(load_seconds):10.2f} ms",
)

print(
    "Payload load setup:",
    f"{ms(load_payload_seconds):10.2f} ms",
)

print(
    "Rename command:",
    f"{ms(rename_seconds):10.2f} ms",
)

print(
    "Move destination create:",
    f"{ms(destination_create_seconds):10.2f} ms",
)

print(
    "Multi-move command:",
    f"{ms(move_seconds):10.2f} ms",
)

print("")
print(
    "Final load-set size:",
    len(after_move_load_set),
)

print("")
print("CORRECTNESS: PASS")

print("")
print("Files:")
print(" ", main_filename)
print(" ", payload_filename)
