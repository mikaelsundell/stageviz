from pxr import Usd, UsdGeom, Sdf, Tf
import stageviz
import tempfile
import os
import uuid
import time
import shutil


# ============================================================================
# Configuration
# ============================================================================

TOTAL_XFORMS = 25000
PAYLOAD_COUNT = 5000

ASSEMBLY_COUNT = 100
GROUPS_PER_ASSEMBLY = 50
LOAD_STRIDE = 4

assert ASSEMBLY_COUNT * GROUPS_PER_ASSEMBLY == PAYLOAD_COUNT

# Deeper synthetic hierarchy used to exercise namespace behavior on a more
# complex tree shape than the regular 100 x 50 automotive fixture.
COMPLEX_BRANCHES = 25
COMPLEX_MODULES_PER_BRANCH = 20
COMPLEX_PARTS_PER_MODULE = 10
COMPLEX_EXTRA_FILLERS = 5000

# Number of items moved together in the command multi-move benchmark.
COMMAND_MOVE_COUNT = 4

# Timeouts are deliberately generous because this script is also intended for
# Debug builds where USD/TBB can be substantially slower.
STAGE_LOAD_TIMEOUT = 60.0
COMMAND_TIMEOUT = 180.0

RUN_NOTICE_REFERENCE_TESTS = True
RUN_25K_COMMAND_TESTS = True
RUN_DIRECT_NAMESPACE_COMPARISON = True
RUN_COMPLEX_HIERARCHY_TESTS = True


# ============================================================================
# Common helpers
# ============================================================================

RESULTS = []


def ms(seconds):
    return seconds * 1000.0


def section(title):
    print("")
    print("=" * 78)
    print(title)
    print("=" * 78)
    print("")


def require(condition, message):
    if not condition:
        raise RuntimeError(message)
    print("[PASS]", message)


def record(category, name, seconds=None, detail="PASS"):
    RESULTS.append(
        {
            "category": category,
            "name": name,
            "seconds": seconds,
            "detail": detail,
        }
    )


def wait_until(predicate, timeout=30.0, interval=0.01):
    started = time.perf_counter()

    while time.perf_counter() - started < timeout:
        if predicate():
            return True

        try:
            stageviz.process_events()
        except Exception:
            pass

        time.sleep(interval)

    return False


def active_stage():
    return stageviz.session().stage()


def exists(path):
    usd_stage = active_stage()
    return bool(usd_stage and usd_stage.GetPrimAtPath(Sdf.Path(path)))


def loaded(path):
    usd_stage = active_stage()

    if not usd_stage:
        return False

    prim = usd_stage.GetPrimAtPath(Sdf.Path(path))
    return bool(prim and prim.IsLoaded())


def get_load_set():
    usd_stage = active_stage()
    return set(usd_stage.GetLoadSet()) if usd_stage else set()


def path_string(path):
    try:
        return path.pathString
    except Exception:
        return str(path)


def is_at_or_below(path, root):
    return path == root or path.HasPrefix(root)


def remap_path(path, old_root, new_root):
    if path == old_root:
        return new_root

    if path.HasPrefix(old_root):
        relative = path.MakeRelativePath(old_root)
        return new_root.AppendPath(relative)

    return path


def print_load_summary(stage, label):
    print("")
    print(label)
    print("  rules   :", len(stage.GetLoadRules().GetRules()))
    print("  load set:", len(stage.GetLoadSet()))


def make_namespace_editor(stage):
    options = Usd.NamespaceEditor.EditOptions()
    options.allowRelocatesAuthoring = False
    return Usd.NamespaceEditor(stage, options)


# ============================================================================
# Fixture helpers
# ============================================================================

def make_payload_file(filename):
    stage = Usd.Stage.CreateNew(filename)

    root = UsdGeom.Xform.Define(stage, "/PayloadRoot")
    stage.SetDefaultPrim(root.GetPrim())

    UsdGeom.Xform.Define(stage, "/PayloadRoot/Geometry")
    cube = UsdGeom.Cube.Define(stage, "/PayloadRoot/Geometry/Box")
    cube.CreateSizeAttr(10.0)

    stage.GetRootLayer().Save()
    return stage


def make_25k_fixture(filename, payload_filename):
    stage = Usd.Stage.CreateNew(filename)

    world = UsdGeom.Xform.Define(stage, "/World")
    stage.SetDefaultPrim(world.GetPrim())

    xform_count = 1
    payload_count = 0

    for assembly_index in range(ASSEMBLY_COUNT):
        assembly_path = f"/World/Assembly_{assembly_index:03d}"
        UsdGeom.Xform.Define(stage, assembly_path)
        xform_count += 1

        for group_index in range(GROUPS_PER_ASSEMBLY):
            group_path = f"{assembly_path}/Group_{group_index:02d}"
            UsdGeom.Xform.Define(stage, group_path)
            xform_count += 1

            payload_path = f"{group_path}/Payload"
            payload = UsdGeom.Xform.Define(stage, payload_path)
            payload.GetPrim().GetPayloads().AddPayload(
                payload_filename,
                "/PayloadRoot",
            )

            xform_count += 1
            payload_count += 1

    filler_index = 0

    while xform_count < TOTAL_XFORMS:
        assembly_index = filler_index % ASSEMBLY_COUNT
        filler_path = (
            f"/World/Assembly_{assembly_index:03d}/"
            f"Filler_{filler_index:05d}"
        )

        UsdGeom.Xform.Define(stage, filler_path)
        xform_count += 1
        filler_index += 1

    require(
        xform_count == TOTAL_XFORMS,
        f"25k fixture contains exactly {TOTAL_XFORMS:,} Xforms",
    )

    require(
        payload_count == PAYLOAD_COUNT,
        f"25k fixture contains exactly {PAYLOAD_COUNT:,} payload arcs",
    )

    stage.GetRootLayer().Save()
    return stage


def make_complex_fixture(filename, payload_filename):
    """
    Create a deeper hierarchy with many nested parents and payload holders.

    Shape:

        /World
            /Vehicle
                /Branch_00
                    /Structure
                        /Module_00
                            /Parts
                                /Part_00
                                    /Payload
                                ...
                        ...
                /Branch_01
                ...

    Extra fillers are distributed below module/parts parents to make the stage
    large enough to exercise namespace processing on a broad + deep hierarchy.
    """
    stage = Usd.Stage.CreateNew(filename)

    world = UsdGeom.Xform.Define(stage, "/World")
    stage.SetDefaultPrim(world.GetPrim())

    UsdGeom.Xform.Define(stage, "/World/Vehicle")

    xform_count = 2
    payload_count = 0

    for branch_index in range(COMPLEX_BRANCHES):
        branch_path = f"/World/Vehicle/Branch_{branch_index:02d}"
        structure_path = f"{branch_path}/Structure"

        UsdGeom.Xform.Define(stage, branch_path)
        UsdGeom.Xform.Define(stage, structure_path)
        xform_count += 2

        for module_index in range(COMPLEX_MODULES_PER_BRANCH):
            module_path = f"{structure_path}/Module_{module_index:02d}"
            parts_path = f"{module_path}/Parts"

            UsdGeom.Xform.Define(stage, module_path)
            UsdGeom.Xform.Define(stage, parts_path)
            xform_count += 2

            for part_index in range(COMPLEX_PARTS_PER_MODULE):
                part_path = f"{parts_path}/Part_{part_index:02d}"
                payload_path = f"{part_path}/Payload"

                UsdGeom.Xform.Define(stage, part_path)
                payload = UsdGeom.Xform.Define(stage, payload_path)

                payload.GetPrim().GetPayloads().AddPayload(
                    payload_filename,
                    "/PayloadRoot",
                )

                xform_count += 2
                payload_count += 1

    filler_index = 0

    while filler_index < COMPLEX_EXTRA_FILLERS:
        branch_index = filler_index % COMPLEX_BRANCHES
        module_index = (
            filler_index // COMPLEX_BRANCHES
        ) % COMPLEX_MODULES_PER_BRANCH

        filler_path = (
            f"/World/Vehicle/Branch_{branch_index:02d}/"
            f"Structure/Module_{module_index:02d}/"
            f"Parts/Filler_{filler_index:05d}"
        )

        UsdGeom.Xform.Define(stage, filler_path)
        xform_count += 1
        filler_index += 1

    stage.GetRootLayer().Save()

    print("Complex fixture Xforms:", f"{xform_count:,}")
    print("Complex fixture payload arcs:", f"{payload_count:,}")

    return stage, xform_count, payload_count


def sparse_payload_paths_25k():
    result = []

    for assembly_index in range(ASSEMBLY_COUNT):
        for group_index in range(GROUPS_PER_ASSEMBLY):
            if group_index % LOAD_STRIDE != 0:
                continue

            result.append(
                (
                    f"/World/Assembly_{assembly_index:03d}/"
                    f"Group_{group_index:02d}/Payload"
                )
            )

    return result


def sparse_payload_paths_complex():
    result = []

    for branch_index in range(COMPLEX_BRANCHES):
        for module_index in range(COMPLEX_MODULES_PER_BRANCH):
            for part_index in range(COMPLEX_PARTS_PER_MODULE):
                if part_index % 4 != 0:
                    continue

                result.append(
                    (
                        f"/World/Vehicle/Branch_{branch_index:02d}/"
                        f"Structure/Module_{module_index:02d}/"
                        f"Parts/Part_{part_index:02d}/Payload"
                    )
                )

    return result


def load_stageviz_fixture(filename, payload_paths):
    session = stageviz.session()

    require(session is not None, "StageViz session available")

    started = time.perf_counter()

    require(
        session.load(filename, stageviz.LoadNone),
        "StageViz loads fixture with LoadNone",
    )

    require(
        wait_until(
            lambda: active_stage() is not None,
            timeout=STAGE_LOAD_TIMEOUT,
        ),
        "StageViz stage becomes available",
    )

    stage_load_seconds = time.perf_counter() - started

    started = time.perf_counter()

    stageviz.command.load_payloads(payload_paths)

    if payload_paths:
        sample_first = payload_paths[0]
        sample_last = payload_paths[-1]

        require(
            wait_until(
                lambda: loaded(sample_first) and loaded(sample_last),
                timeout=COMMAND_TIMEOUT,
            ),
            "StageViz sparse payload loads complete",
        )

    payload_load_seconds = time.perf_counter() - started

    return stage_load_seconds, payload_load_seconds


# ============================================================================
# Safe notice recording
# ============================================================================

class NoticeRecorder:
    """
    Notice recorder for edits executed on the Python thread.

    Do not attach this to a StageViz stage while executing threaded StageViz
    namespace commands; a Python Tf.Notice callback can deadlock on the GIL.
    """

    def __init__(self, stage):
        self.records = []
        self.key = Tf.Notice.Register(
            Usd.Notice.ObjectsChanged,
            self._on_notice,
            stage,
        )

    def _on_notice(self, notice, sender):
        record = []

        for path in notice.GetResyncedPaths():
            record.append(
                (
                    path_string(path),
                    notice.GetPrimResyncType(path),
                )
            )

        self.records.append(record)

    def entries(self):
        result = []

        for record in self.records:
            result.extend(record)

        return result

    def close(self):
        self.key = None


def notice_entry(entries, path):
    for entry_path, value in entries:
        if entry_path == path:
            return value

    return None


# ============================================================================
# Test 1: backend notice semantics
# ============================================================================

def run_notice_reference_tests(temp_dir, run_id):
    section("TEST 1 - NAMESPACE NOTICE SEMANTICS")

    rename_filename = os.path.join(
        temp_dir,
        f"notice_rename_{run_id}.usda",
    )

    move_filename = os.path.join(
        temp_dir,
        f"notice_move_{run_id}.usda",
    )

    def make_fixture(filename):
        stage = Usd.Stage.CreateNew(filename)

        world = UsdGeom.Xform.Define(stage, "/World")
        stage.SetDefaultPrim(world.GetPrim())

        UsdGeom.Xform.Define(stage, "/World/Source")
        UsdGeom.Xform.Define(stage, "/World/Destination")
        UsdGeom.Xform.Define(stage, "/World/Source/RenameMe")
        UsdGeom.Xform.Define(stage, "/World/Source/MoveMe")
        UsdGeom.Xform.Define(stage, "/World/Source/MoveMe/Child")

        stage.GetRootLayer().Save()

    make_fixture(rename_filename)
    make_fixture(move_filename)

    # Rename reference: UsdNamespaceEditor.
    rename_stage = Usd.Stage.Open(rename_filename)
    rename_recorder = NoticeRecorder(rename_stage)

    old_path = Sdf.Path("/World/Source/RenameMe")
    new_path = Sdf.Path("/World/Source/Renamed")

    editor = make_namespace_editor(rename_stage)

    started = time.perf_counter()

    require(
        editor.RenamePrim(
            rename_stage.GetPrimAtPath(old_path),
            new_path.name,
        ),
        "UsdNamespaceEditor queues rename",
    )

    require(
        editor.ApplyEdits(),
        "UsdNamespaceEditor applies rename",
    )

    rename_seconds = time.perf_counter() - started
    rename_entries = rename_recorder.entries()
    rename_recorder.close()

    old_notice = notice_entry(rename_entries, old_path.pathString)
    new_notice = notice_entry(rename_entries, new_path.pathString)

    print(" ", old_path, "=>", old_notice)
    print(" ", new_path, "=>", new_notice)

    require(
        old_notice is not None
        and old_notice[0]
        == Usd.Notice.ObjectsChanged.PrimResyncType.RenameSource,
        "rename old path is RenameSource",
    )

    require(
        new_notice is not None
        and new_notice[0]
        == Usd.Notice.ObjectsChanged.PrimResyncType.RenameDestination,
        "rename new path is RenameDestination",
    )

    require(
        old_notice[1] == new_path,
        "RenameSource contains destination path",
    )

    require(
        new_notice[1] == old_path,
        "RenameDestination contains source path",
    )

    record(
        "notice",
        "UsdNamespaceEditor rename notice",
        rename_seconds,
    )

    # Move reference: SdfBatchNamespaceEdit.
    move_stage = Usd.Stage.Open(move_filename)
    move_recorder = NoticeRecorder(move_stage)

    old_move = Sdf.Path("/World/Source/MoveMe")
    new_move = Sdf.Path("/World/Destination/MoveMe")

    layer = move_stage.GetEditTarget().GetLayer()
    batch = Sdf.BatchNamespaceEdit()
    batch.Add(old_move, new_move)

    started = time.perf_counter()

    require(
        bool(layer.CanApply(batch)),
        "SdfBatchNamespaceEdit move can apply",
    )

    require(
        bool(layer.Apply(batch)),
        "SdfBatchNamespaceEdit move applies",
    )

    move_seconds = time.perf_counter() - started
    move_entries = move_recorder.entries()
    move_recorder.close()

    old_move_notice = notice_entry(
        move_entries,
        old_move.pathString,
    )

    new_move_notice = notice_entry(
        move_entries,
        new_move.pathString,
    )

    print(" ", old_move, "=>", old_move_notice)
    print(" ", new_move, "=>", new_move_notice)

    require(
        old_move_notice is not None
        and old_move_notice[0]
        == Usd.Notice.ObjectsChanged.PrimResyncType.Delete,
        "move old path is Delete",
    )

    require(
        new_move_notice is not None
        and new_move_notice[0]
        == Usd.Notice.ObjectsChanged.PrimResyncType.Other,
        "move new path is Other",
    )

    record(
        "notice",
        "SdfBatchNamespaceEdit move notice",
        move_seconds,
    )


# ============================================================================
# Test 2: 25k StageViz command implementation
# ============================================================================

def run_25k_command_tests(main_filename):
    section("TEST 2 - 25K STAGEVIZ COMMAND PERFORMANCE / CORRECTNESS")

    payloads_to_load = sparse_payload_paths_25k()

    stage_load_seconds, payload_load_seconds = load_stageviz_fixture(
        main_filename,
        payloads_to_load,
    )

    print_load_summary(
        active_stage(),
        "25k StageViz working set:",
    )

    baseline_load_set = get_load_set()

    require(
        len(baseline_load_set) == len(payloads_to_load),
        "baseline load-set size matches explicit payload loads",
    )

    # Rename through the public command API.
    rename_old = Sdf.Path("/World/Assembly_050/Group_08")
    rename_new = Sdf.Path("/World/Assembly_050/Group_08_Renamed")
    rename_old_payload = rename_old.AppendChild("Payload")
    rename_new_payload = rename_new.AppendChild("Payload")

    require(
        loaded(rename_old_payload.pathString),
        "rename payload is loaded before rename",
    )

    started = time.perf_counter()

    stageviz.command.rename_path(
        rename_old.pathString,
        rename_new.name,
    )

    rename_dispatch_seconds = time.perf_counter() - started

    require(
        wait_until(
            lambda: (
                not exists(rename_old.pathString)
                and exists(rename_new.pathString)
                and loaded(rename_new_payload.pathString)
            ),
            timeout=COMMAND_TIMEOUT,
        ),
        "StageViz rename completes and preserves loaded payload",
    )

    rename_stable_seconds = time.perf_counter() - started

    after_rename_load_set = get_load_set()

    require(
        rename_old_payload not in after_rename_load_set,
        "old renamed payload path leaves load set",
    )

    require(
        rename_new_payload in after_rename_load_set,
        "new renamed payload path enters load set",
    )

    # Destination for command multi-move.
    destination = Sdf.Path("/World/MoveDestination")

    started = time.perf_counter()

    stageviz.command.new_xform(
        "/World",
        "MoveDestination",
    )

    require(
        wait_until(
            lambda: exists(destination.pathString),
            timeout=COMMAND_TIMEOUT,
        ),
        "move destination created through StageViz command",
    )

    destination_seconds = time.perf_counter() - started

    stageviz.command.select_paths([])

    move_roots = [
        Sdf.Path("/World/Assembly_000/Group_00"),
        Sdf.Path("/World/Assembly_000/Group_04"),
        Sdf.Path("/World/Assembly_000/Group_01"),
        Sdf.Path("/World/Assembly_000/Group_02"),
    ]

    move_new = [
        destination.AppendChild(path.name)
        for path in move_roots
    ]

    loaded_old = [
        move_roots[0].AppendChild("Payload"),
        move_roots[1].AppendChild("Payload"),
    ]

    unloaded_old = [
        move_roots[2].AppendChild("Payload"),
        move_roots[3].AppendChild("Payload"),
    ]

    loaded_new = [
        move_new[0].AppendChild("Payload"),
        move_new[1].AppendChild("Payload"),
    ]

    unloaded_new = [
        move_new[2].AppendChild("Payload"),
        move_new[3].AppendChild("Payload"),
    ]

    require(
        all(path in get_load_set() for path in loaded_old),
        "loaded move payloads are loaded before move",
    )

    require(
        all(path not in get_load_set() for path in unloaded_old),
        "unloaded move payloads are unloaded before move",
    )

    unrelated = Sdf.Path(
        "/World/Assembly_010/Group_08/Payload"
    )

    require(
        unrelated in get_load_set(),
        "unrelated loaded payload exists before move",
    )

    started = time.perf_counter()

    stageviz.command.move_path(
        [path.pathString for path in move_roots],
        destination.pathString,
        insert_index=0,
        preserve_world_transform=True,
    )

    move_dispatch_seconds = time.perf_counter() - started

    require(
        wait_until(
            lambda: (
                all(not exists(path.pathString) for path in move_roots)
                and all(exists(path.pathString) for path in move_new)
            ),
            timeout=COMMAND_TIMEOUT,
        ),
        "StageViz multi-move completes",
    )

    move_stable_seconds = time.perf_counter() - started
    after_move_load_set = get_load_set()

    require(
        all(path not in after_move_load_set for path in loaded_old),
        "old loaded payload paths leave load set",
    )

    require(
        all(path in after_move_load_set for path in loaded_new),
        "new loaded payload paths enter load set",
    )

    require(
        all(path not in after_move_load_set for path in unloaded_new),
        "unloaded moved payloads remain absent from load set",
    )

    require(
        all(not loaded(path.pathString) for path in unloaded_new),
        "unloaded moved payloads remain unloaded",
    )

    require(
        unrelated in after_move_load_set,
        "unrelated loaded payload remains loaded",
    )

    require(
        len(after_move_load_set) == len(baseline_load_set),
        "namespace edits preserve total load-set size",
    )

    print("")
    print("25K timings:")
    print("  session.load:          ", f"{ms(stage_load_seconds):10.2f} ms")
    print("  load_payloads:         ", f"{ms(payload_load_seconds):10.2f} ms")
    print("  rename dispatch:       ", f"{ms(rename_dispatch_seconds):10.2f} ms")
    print("  rename stable:         ", f"{ms(rename_stable_seconds):10.2f} ms")
    print("  destination create:    ", f"{ms(destination_seconds):10.2f} ms")
    print("  multi-move dispatch:   ", f"{ms(move_dispatch_seconds):10.2f} ms")
    print("  multi-move stable:     ", f"{ms(move_stable_seconds):10.2f} ms")

    record("25k command", "session.load", stage_load_seconds)
    record("25k command", "load_payloads", payload_load_seconds)
    record("25k command", "rename stable", rename_stable_seconds)
    record("25k command", "multi-move stable", move_stable_seconds)


# ============================================================================
# Test 3: direct USD namespace comparison on 25k stage
# ============================================================================

def run_direct_namespace_comparison(main_filename, temp_dir, run_id):
    section("TEST 3 - DIRECT USD NAMESPACE PERFORMANCE COMPARISON")

    def fresh_copy(label):
        filename = os.path.join(
            temp_dir,
            f"direct_{label}_{run_id}.usda",
        )

        shutil.copyfile(
            main_filename,
            filename,
        )

        return filename

    # Single rename with CanApplyEdits.
    stage = Usd.Stage.Open(
        fresh_copy("rename_with_canapply"),
        load=Usd.Stage.LoadNone,
    )

    rename_old = Sdf.Path("/World/Assembly_050/Group_08")
    rename_new = Sdf.Path("/World/Assembly_050/Group_08_Direct")

    editor = make_namespace_editor(stage)

    started = time.perf_counter()

    require(
        editor.RenamePrim(
            stage.GetPrimAtPath(rename_old),
            rename_new.name,
        ),
        "direct USD rename queues",
    )

    queue_seconds = time.perf_counter() - started

    started = time.perf_counter()
    can_apply = editor.CanApplyEdits()
    can_seconds = time.perf_counter() - started

    require(
        bool(can_apply),
        "direct USD rename CanApplyEdits succeeds",
    )

    started = time.perf_counter()
    applied = editor.ApplyEdits()
    apply_seconds = time.perf_counter() - started

    require(
        bool(applied),
        "direct USD rename ApplyEdits succeeds",
    )

    require(
        not stage.GetPrimAtPath(rename_old)
        and stage.GetPrimAtPath(rename_new),
        "direct USD rename namespace result is correct",
    )

    print("")
    print("Direct UsdNamespaceEditor rename:")
    print("  queue:       ", f"{ms(queue_seconds):10.2f} ms")
    print("  CanApply:    ", f"{ms(can_seconds):10.2f} ms")
    print("  Apply:       ", f"{ms(apply_seconds):10.2f} ms")

    record(
        "direct USD",
        "UsdNamespaceEditor rename CanApply",
        can_seconds,
    )

    record(
        "direct USD",
        "UsdNamespaceEditor rename Apply",
        apply_seconds,
    )

    # Same valid rename without CanApplyEdits.
    stage = Usd.Stage.Open(
        fresh_copy("rename_apply_only"),
        load=Usd.Stage.LoadNone,
    )

    editor = make_namespace_editor(stage)

    require(
        editor.RenamePrim(
            stage.GetPrimAtPath(rename_old),
            rename_new.name,
        ),
        "direct USD rename queues without CanApply",
    )

    started = time.perf_counter()
    applied = editor.ApplyEdits()
    apply_without_can_seconds = time.perf_counter() - started

    require(
        bool(applied),
        "direct USD rename ApplyEdits succeeds without CanApply",
    )

    print(
        "  Apply without CanApply:",
        f"{ms(apply_without_can_seconds):10.2f} ms",
    )

    record(
        "direct USD",
        "UsdNamespaceEditor rename Apply-only",
        apply_without_can_seconds,
    )

    # True Sdf batch multi-move on a fresh stage.
    stage = Usd.Stage.Open(
        fresh_copy("sdf_batch_move"),
        load=Usd.Stage.LoadNone,
    )

    layer = stage.GetEditTarget().GetLayer()

    moves = [
        (
            Sdf.Path("/World/Assembly_000/Group_00"),
            Sdf.Path("/World/Assembly_099/Group_00_Moved"),
        ),
        (
            Sdf.Path("/World/Assembly_000/Group_04"),
            Sdf.Path("/World/Assembly_099/Group_04_Moved"),
        ),
        (
            Sdf.Path("/World/Assembly_001/Group_00"),
            Sdf.Path("/World/Assembly_098/Group_00_Moved"),
        ),
        (
            Sdf.Path("/World/Assembly_001/Group_04"),
            Sdf.Path("/World/Assembly_098/Group_04_Moved"),
        ),
    ]

    batch = Sdf.BatchNamespaceEdit()

    for old_path, new_path in moves:
        batch.Add(old_path, new_path)

    started = time.perf_counter()
    can_apply = layer.CanApply(batch)
    batch_can_seconds = time.perf_counter() - started

    require(
        bool(can_apply),
        "direct Sdf batch multi-move can apply",
    )

    started = time.perf_counter()
    applied = layer.Apply(batch)
    batch_apply_seconds = time.perf_counter() - started

    require(
        bool(applied),
        "direct Sdf batch multi-move applies",
    )

    require(
        all(
            not stage.GetPrimAtPath(old_path)
            and stage.GetPrimAtPath(new_path)
            for old_path, new_path in moves
        ),
        "direct Sdf batch namespace result is correct",
    )

    print("")
    print("Direct SdfBatchNamespaceEdit multi-move:")
    print("  items:       ", len(moves))
    print("  CanApply:    ", f"{ms(batch_can_seconds):10.2f} ms")
    print("  Apply:       ", f"{ms(batch_apply_seconds):10.2f} ms")

    record(
        "direct USD",
        "Sdf batch multi-move CanApply",
        batch_can_seconds,
    )

    record(
        "direct USD",
        "Sdf batch multi-move Apply",
        batch_apply_seconds,
    )


# ============================================================================
# Test 4: deeper/complex hierarchy
# ============================================================================

def run_complex_hierarchy_tests(main_filename):
    section("TEST 4 - COMPLEX HIERARCHY COMMAND / NAMESPACE PERFORMANCE")

    payloads_to_load = sparse_payload_paths_complex()

    stage_load_seconds, payload_load_seconds = load_stageviz_fixture(
        main_filename,
        payloads_to_load,
    )

    baseline_load_set = get_load_set()

    # Rename a module with many descendants. This intentionally exercises the
    # native UsdNamespaceEditor path used by edit::NamespaceEditor::renamePrim.
    rename_old = Sdf.Path(
        "/World/Vehicle/Branch_10/Structure/Module_08"
    )

    rename_new = Sdf.Path(
        "/World/Vehicle/Branch_10/Structure/Module_08_Renamed"
    )

    started = time.perf_counter()

    stageviz.command.rename_path(
        rename_old.pathString,
        rename_new.name,
    )

    require(
        wait_until(
            lambda: (
                not exists(rename_old.pathString)
                and exists(rename_new.pathString)
            ),
            timeout=COMMAND_TIMEOUT,
        ),
        "complex hierarchy rename completes",
    )

    complex_rename_seconds = time.perf_counter() - started

    # Create a destination branch and move several modules at once. This is the
    # workload that should benefit from the new true Sdf batching.
    destination = Sdf.Path(
        "/World/Vehicle/Branch_24/Structure/MoveDestination"
    )

    stageviz.command.new_xform(
        "/World/Vehicle/Branch_24/Structure",
        "MoveDestination",
    )

    require(
        wait_until(
            lambda: exists(destination.pathString),
            timeout=COMMAND_TIMEOUT,
        ),
        "complex move destination created",
    )

    stageviz.command.select_paths([])

    move_roots = [
        Sdf.Path("/World/Vehicle/Branch_00/Structure/Module_00"),
        Sdf.Path("/World/Vehicle/Branch_00/Structure/Module_01"),
        Sdf.Path("/World/Vehicle/Branch_01/Structure/Module_00"),
        Sdf.Path("/World/Vehicle/Branch_01/Structure/Module_01"),
        Sdf.Path("/World/Vehicle/Branch_02/Structure/Module_00"),
        Sdf.Path("/World/Vehicle/Branch_02/Structure/Module_01"),
        Sdf.Path("/World/Vehicle/Branch_03/Structure/Module_00"),
        Sdf.Path("/World/Vehicle/Branch_03/Structure/Module_01"),
    ]

    move_new = [
        destination.AppendChild(path.name)
        for path in move_roots
    ]

    # Destination child names must be unique in one parent. Rename duplicate
    # module names deterministically while keeping the test a true batch.
    move_new = [
        destination.AppendChild(
            f"Moved_{index:02d}_{path.name}"
        )
        for index, path in enumerate(move_roots)
    ]

    # StageViz command.move_path preserves names, so the actual command can only
    # move sources with unique names into one parent. Choose unique module names
    # for the command test.
    move_roots = [
        Sdf.Path("/World/Vehicle/Branch_00/Structure/Module_00"),
        Sdf.Path("/World/Vehicle/Branch_00/Structure/Module_01"),
        Sdf.Path("/World/Vehicle/Branch_00/Structure/Module_02"),
        Sdf.Path("/World/Vehicle/Branch_00/Structure/Module_03"),
        Sdf.Path("/World/Vehicle/Branch_00/Structure/Module_04"),
        Sdf.Path("/World/Vehicle/Branch_00/Structure/Module_05"),
        Sdf.Path("/World/Vehicle/Branch_00/Structure/Module_06"),
        Sdf.Path("/World/Vehicle/Branch_00/Structure/Module_07"),
    ]

    move_new = [
        destination.AppendChild(path.name)
        for path in move_roots
    ]

    started = time.perf_counter()

    stageviz.command.move_path(
        [path.pathString for path in move_roots],
        destination.pathString,
        insert_index=0,
        preserve_world_transform=True,
    )

    require(
        wait_until(
            lambda: (
                all(not exists(path.pathString) for path in move_roots)
                and all(exists(path.pathString) for path in move_new)
            ),
            timeout=COMMAND_TIMEOUT,
        ),
        "complex hierarchy 8-item multi-move completes",
    )

    complex_move_seconds = time.perf_counter() - started
    after_load_set = get_load_set()

    require(
        len(after_load_set) == len(baseline_load_set),
        "complex namespace edits preserve total load-set size",
    )

    print("")
    print("Complex hierarchy timings:")
    print("  session.load:       ", f"{ms(stage_load_seconds):10.2f} ms")
    print("  load_payloads:      ", f"{ms(payload_load_seconds):10.2f} ms")
    print("  module rename:      ", f"{ms(complex_rename_seconds):10.2f} ms")
    print("  8-item multi-move:  ", f"{ms(complex_move_seconds):10.2f} ms")

    record(
        "complex command",
        "session.load",
        stage_load_seconds,
    )

    record(
        "complex command",
        "load_payloads",
        payload_load_seconds,
    )

    record(
        "complex command",
        "module rename",
        complex_rename_seconds,
    )

    record(
        "complex command",
        "8-item multi-move",
        complex_move_seconds,
    )


# ============================================================================
# Main
# ============================================================================

run_id = uuid.uuid4().hex[:8]

temp_dir = os.path.join(
    tempfile.gettempdir(),
    "stageviz_namespace_validation_v3",
)

os.makedirs(
    temp_dir,
    exist_ok=True,
)

payload_filename = os.path.join(
    temp_dir,
    f"payload_{run_id}.usda",
)

main_25k_filename = os.path.join(
    temp_dir,
    f"main_25k_command_{run_id}.usda",
)

main_25k_direct_filename = os.path.join(
    temp_dir,
    f"main_25k_direct_{run_id}.usda",
)

complex_filename = os.path.join(
    temp_dir,
    f"main_complex_{run_id}.usda",
)


section("CREATE COMMON STAGEVIZ NAMESPACE TEST FIXTURES")

started = time.perf_counter()

payload_stage = make_payload_file(payload_filename)
main_25k_stage = make_25k_fixture(
    main_25k_filename,
    payload_filename,
)

main_25k_direct_stage = make_25k_fixture(
    main_25k_direct_filename,
    payload_filename,
)

complex_stage, complex_xforms, complex_payloads = make_complex_fixture(
    complex_filename,
    payload_filename,
)

fixture_seconds = time.perf_counter() - started

print(
    "Combined fixture creation:",
    f"{ms(fixture_seconds):.2f} ms",
)

record(
    "fixture",
    "combined fixture creation",
    fixture_seconds,
)

payload_stage = None
main_25k_stage = None
main_25k_direct_stage = None
complex_stage = None


if RUN_NOTICE_REFERENCE_TESTS:
    run_notice_reference_tests(
        temp_dir,
        run_id,
    )

if RUN_25K_COMMAND_TESTS:
    run_25k_command_tests(
        main_25k_filename,
    )

if RUN_DIRECT_NAMESPACE_COMPARISON:
    run_direct_namespace_comparison(
        main_25k_direct_filename,
        temp_dir,
        run_id,
    )

if RUN_COMPLEX_HIERARCHY_TESTS:
    run_complex_hierarchy_tests(
        complex_filename,
    )


# ============================================================================
# Final summary
# ============================================================================

section("FINAL STAGEVIZ NAMESPACE VALIDATION SUMMARY")

print(
    "{:<18} {:<42} {:>12}".format(
        "Category",
        "Test",
        "Time",
    )
)

print(
    "{:<18} {:<42} {:>12}".format(
        "-" * 18,
        "-" * 42,
        "-" * 12,
    )
)

for item in RESULTS:
    seconds = item["seconds"]

    if seconds is None:
        timing = item["detail"]
    else:
        timing = f"{ms(seconds):.2f} ms"

    print(
        "{:<18} {:<42} {:>12}".format(
            item["category"],
            item["name"],
            timing,
        )
    )

print("")
print("Expected notice contract:")
print("  rename old -> RenameSource(new)")
print("  rename new -> RenameDestination(old)")
print("  move old   -> Delete")
print("  move new   -> Other")

print("")
print("Expected command contract:")
print("  rename uses edit::NamespaceEditor and preserves semantic rename notices")
print("  multi-move uses edit::NamespaceEditor and true Sdf batching")
print("  loaded descendants remap to moved/renamed paths")
print("  unloaded descendants remain unloaded")
print("  unrelated payload working-set state remains unchanged")
print("  multi-move correctness is judged by USD hierarchy/load state, not tree expansion")

print("")
print("NOTE:")
print("  Exact ObjectsChanged classification for threaded StageViz commands is")
print("  intentionally not captured with a Python Tf.Notice callback because")
print("  callback delivery can deadlock on the Python GIL. The backend notice")
print("  semantics are validated safely on the Python thread instead.")
print("  StageTree expansion/collapse and selection-follow behavior are intentionally")
print("  excluded from this namespace benchmark; multi-move may collapse in the UI.")

print("")
print("VALIDATION: PASS")

print("")
print("Files:")
print("  25k command:", main_25k_filename)
print("  25k direct: ", main_25k_direct_filename)
print("  complex:", complex_filename)
print("  payload:", payload_filename)
