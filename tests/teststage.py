from pxr import Usd, UsdGeom, Sdf
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

# 100 * 50 = 5,000 payload holders.
assert ASSEMBLY_COUNT * GROUPS_PER_ASSEMBLY == PAYLOAD_COUNT

# Load one payload out of every LOAD_STRIDE groups.
# 50 groups / assembly with stride 4 => 13 loaded payloads per assembly
# => roughly 1,300 explicit loaded payload paths across the stage.
LOAD_STRIDE = 4

# Two authored group hierarchies to move, matching the StageViz multi-move case.
MOVE_SPECS = [
    (
        Sdf.Path("/World/Assembly_000/Group_00"),
        Sdf.Path("/World/Assembly_099/Group_00_Moved"),
    ),
    (
        Sdf.Path("/World/Assembly_000/Group_04"),
        Sdf.Path("/World/Assembly_099/Group_04_Moved"),
    ),
]


# ============================================================================
# Utility
# ============================================================================

def ms(seconds):
    return seconds * 1000.0


def path_text(path):
    return path.pathString


def is_at_or_below(path, root):
    return path == root or path.HasPrefix(root)


def remap_path(path, old_root, new_root):
    if path == old_root:
        return new_root

    if path.HasPrefix(old_root):
        relative = path.MakeRelativePath(old_root)
        return new_root.AppendPath(relative)

    return path


def print_rule_summary(stage, label):
    rules = stage.GetLoadRules().GetRules()
    load_set = stage.GetLoadSet()

    print("")
    print(label)
    print("  rules   :", len(rules))
    print("  load set:", len(load_set))


def print_paths(label, paths, max_count=20):
    ordered = sorted(
        paths,
        key=lambda p: p.pathString,
    )

    print("")
    print(label, len(ordered))

    for path in ordered[:max_count]:
        print(" ", path)

    if len(ordered) > max_count:
        print(
            "  ...",
            len(ordered) - max_count,
            "more",
        )


# ============================================================================
# Fixture creation
# ============================================================================

def make_payload_file(filename):
    """
    One external payload asset reused by all 5,000 payload holders.

    Reusing one asset keeps filesystem overhead low while still creating
    5,000 independent payload arcs in the composed main stage.
    """
    stage = Usd.Stage.CreateNew(filename)

    root = UsdGeom.Xform.Define(
        stage,
        "/PayloadRoot",
    )

    stage.SetDefaultPrim(
        root.GetPrim()
    )

    UsdGeom.Xform.Define(
        stage,
        "/PayloadRoot/Geometry",
    )

    cube = UsdGeom.Cube.Define(
        stage,
        "/PayloadRoot/Geometry/Box",
    )

    cube.CreateSizeAttr(10.0)

    stage.GetRootLayer().Save()

    return stage


def make_main_stage(
    filename,
    payload_filename,
):
    """
    Create a root-layer-authored hierarchy suitable for namespace editing with:

        allowRelocatesAuthoring = False

    Important:
        The moved Group prims, their parents, and destination parent are all
        authored strongest in the main/root layer. The payload arc exists on a
        child Payload prim below each Group.

    Shape:

        /World
            /Assembly_000
                /Group_00
                    /Payload   <-- authored Xform + payload arc
                /Group_01
                    /Payload
                ...
            ...
            /Assembly_099
                ...

    Additional filler Xforms are added until the stage has exactly
    TOTAL_XFORMS authored Xforms in the main layer.
    """
    stage = Usd.Stage.CreateNew(filename)

    world = UsdGeom.Xform.Define(
        stage,
        "/World",
    )

    stage.SetDefaultPrim(
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
            stage,
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
                stage,
                group_path,
            )

            xform_count += 1

            payload_path = (
                f"{group_path}/Payload"
            )

            payload = UsdGeom.Xform.Define(
                stage,
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

        assembly_path = (
            f"/World/"
            f"Assembly_{assembly_index:03d}"
        )

        filler_path = (
            f"{assembly_path}/"
            f"Filler_{filler_index:05d}"
        )

        UsdGeom.Xform.Define(
            stage,
            filler_path,
        )

        xform_count += 1
        filler_index += 1

    if xform_count != TOTAL_XFORMS:
        raise RuntimeError(
            f"Expected {TOTAL_XFORMS} Xforms, got {xform_count}"
        )

    if payload_count != PAYLOAD_COUNT:
        raise RuntimeError(
            f"Expected {PAYLOAD_COUNT} payloads, got {payload_count}"
        )

    stage.GetRootLayer().Save()

    return (
        stage,
        xform_count,
        payload_count,
    )


# ============================================================================
# StageViz-like load setup
# ============================================================================

def open_stageviz_style_stage(
    main_filename,
):
    """
    Mirror StageViz's working-set behavior:

        - Open with LoadNone.
        - Everything is unloaded by default.
        - Explicitly load a sparse set of payload paths.
    """
    started = time.perf_counter()

    stage = Usd.Stage.Open(
        main_filename,
        load=Usd.Stage.LoadNone,
    )

    open_seconds = (
        time.perf_counter()
        - started
    )

    print("")
    print(
        "Stage opened LoadNone:",
        f"{ms(open_seconds):.2f} ms",
    )

    print_rule_summary(
        stage,
        "Initial StageViz-style working set:",
    )

    started = time.perf_counter()

    explicit_loaded = []

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

            path = Sdf.Path(
                (
                    f"/World/"
                    f"Assembly_{assembly_index:03d}/"
                    f"Group_{group_index:02d}/"
                    f"Payload"
                )
            )

            stage.Load(
                path,
                Usd.LoadWithoutDescendants,
            )

            explicit_loaded.append(path)

    load_seconds = (
        time.perf_counter()
        - started
    )

    print("")
    print(
        "Explicit payload loads:",
        len(explicit_loaded),
    )

    print(
        "Load setup time:",
        f"{ms(load_seconds):.2f} ms",
    )

    print_rule_summary(
        stage,
        "After explicit StageViz-style loads:",
    )

    return (
        stage,
        explicit_loaded,
    )


# ============================================================================
# Namespace editing
# ============================================================================

def validate_move_fixture(
    stage,
    moves,
):
    """
    Verify this fixture matches the structural editing assumptions in StageViz.
    """
    print("")
    print("Validating move fixture...")

    edit_layer = (
        stage.GetEditTarget().GetLayer()
    )

    if not edit_layer:
        raise RuntimeError(
            "Missing edit layer"
        )

    for old_path, new_path in moves:
        old_prim = stage.GetPrimAtPath(
            old_path
        )

        new_parent = stage.GetPrimAtPath(
            new_path.GetParentPath()
        )

        if not old_prim:
            raise RuntimeError(
                f"Move source missing: {old_path}"
            )

        if not new_parent:
            raise RuntimeError(
                f"Move destination parent missing: "
                f"{new_path.GetParentPath()}"
            )

        old_stack = old_prim.GetPrimStack()
        parent_stack = (
            new_parent.GetPrimStack()
        )

        if (
            not old_stack
            or old_stack[0].layer != edit_layer
        ):
            raise RuntimeError(
                f"Move source is not strongest in edit layer: "
                f"{old_path}"
            )

        if (
            not parent_stack
            or parent_stack[0].layer != edit_layer
        ):
            raise RuntimeError(
                f"Destination parent is not strongest "
                f"in edit layer: "
                f"{new_path.GetParentPath()}"
            )

        if stage.GetPrimAtPath(new_path):
            raise RuntimeError(
                f"Destination already exists: "
                f"{new_path}"
            )

        print(
            "  OK:",
            old_path,
            "->",
            new_path,
        )


def namespace_move_timed(
    stage,
    old_path,
    new_path,
    label,
):
    """
    Mirror StageViz's current per-move namespace editor flow.

    A fresh editor is used for each move so USD can emit the precise namespace
    change for that move.
    """
    options = (
        Usd.NamespaceEditor.EditOptions()
    )

    options.allowRelocatesAuthoring = False

    editor = Usd.NamespaceEditor(
        stage,
        options,
    )

    started = time.perf_counter()

    ok = editor.MovePrimAtPath(
        old_path,
        new_path,
    )

    queue_seconds = (
        time.perf_counter()
        - started
    )

    print("")
    print(
        f"[{label}] MovePrimAtPath:",
        f"{ms(queue_seconds):.2f} ms",
        "OK" if ok else "FAILED",
    )

    if not ok:
        raise RuntimeError(
            f"MovePrimAtPath failed: "
            f"{old_path} -> {new_path}"
        )

    started = time.perf_counter()

    can_apply = editor.CanApplyEdits()

    can_apply_seconds = (
        time.perf_counter()
        - started
    )

    print(
        f"[{label}] CanApplyEdits:",
        f"{ms(can_apply_seconds):.2f} ms",
        "OK" if bool(can_apply) else "FAILED",
    )

    if not can_apply:
        print(
            f"[{label}] CanApplyEdits result:",
            can_apply,
        )

        raise RuntimeError(
            f"CanApplyEdits failed: "
            f"{old_path} -> {new_path}"
        )

    started = time.perf_counter()

    applied = editor.ApplyEdits()

    apply_seconds = (
        time.perf_counter()
        - started
    )

    print(
        f"[{label}] ApplyEdits:",
        f"{ms(apply_seconds):.2f} ms",
        "OK" if applied else "FAILED",
    )

    if not applied:
        raise RuntimeError(
            f"ApplyEdits failed: "
            f"{old_path} -> {new_path}"
        )

    return {
        "queue": queue_seconds,
        "can_apply": can_apply_seconds,
        "apply": apply_seconds,
        "total": (
            queue_seconds
            + can_apply_seconds
            + apply_seconds
        ),
    }


# ============================================================================
# Loaded-path capture / remap
# ============================================================================

def collect_loaded_paths_under_moves(
    stage,
    moves,
):
    """
    Capture only loaded payload paths at or below the moved roots.

    Because the stage starts from LoadNone, unloaded is already the global
    default. We therefore only need to migrate paths that are actually loaded.
    """
    load_set = stage.GetLoadSet()

    affected = []

    for old_root, new_root in moves:
        loaded = {
            path
            for path in load_set
            if is_at_or_below(
                path,
                old_root,
            )
        }

        affected.append(
            (
                old_root,
                new_root,
                loaded,
            )
        )

    return affected


def build_remapped_loaded_paths(
    affected_loaded,
):
    remapped = set()

    for (
        old_root,
        new_root,
        loaded_paths,
    ) in affected_loaded:

        for old_loaded_path in loaded_paths:
            remapped.add(
                remap_path(
                    old_loaded_path,
                    old_root,
                    new_root,
                )
            )

    return remapped


def restore_targeted_loads(
    stage,
    remapped_loaded_paths,
):
    """
    Restore only the loaded moved paths.

    LoadWithoutDescendants is deliberate:
        a nested payload that was loaded is independently present in the
        captured load set and will be restored explicitly. Unloaded nested
        payloads therefore remain unloaded.
    """
    started = time.perf_counter()

    stage.LoadAndUnload(
        remapped_loaded_paths,
        set(),
        Usd.LoadWithoutDescendants,
    )

    return (
        time.perf_counter()
        - started
    )


# ============================================================================
# Correctness validation
# ============================================================================

def expected_moved_payload_paths(
    moves,
):
    expected = []

    for old_root, new_root in moves:
        old_payload = old_root.AppendChild(
            "Payload"
        )

        new_payload = new_root.AppendChild(
            "Payload"
        )

        expected.append(
            (
                old_payload,
                new_payload,
            )
        )

    return expected


def verify_namespace_result(
    stage,
    moves,
):
    for old_path, new_path in moves:
        if stage.GetPrimAtPath(old_path):
            raise RuntimeError(
                f"Old move path still exists: "
                f"{old_path}"
            )

        if not stage.GetPrimAtPath(new_path):
            raise RuntimeError(
                f"New move path missing: "
                f"{new_path}"
            )


def verify_moved_payload_loads(
    stage,
    expected_payloads,
):
    print("")
    print("Moved payload state:")

    for old_payload, new_payload in (
        expected_payloads
    ):
        old_prim = stage.GetPrimAtPath(
            old_payload
        )

        new_prim = stage.GetPrimAtPath(
            new_payload
        )

        print(
            " ",
            old_payload,
            "exists=",
            bool(old_prim),
            "->",
            new_payload,
            "exists=",
            bool(new_prim),
            "loaded=",
            (
                new_prim.IsLoaded()
                if new_prim
                else False
            ),
        )

        if old_prim:
            raise RuntimeError(
                f"Old payload path still exists: "
                f"{old_payload}"
            )

        if not new_prim:
            raise RuntimeError(
                f"Moved payload missing: "
                f"{new_payload}"
            )

        if not new_prim.HasPayload():
            raise RuntimeError(
                f"Moved prim lost payload arc: "
                f"{new_payload}"
            )

        if not new_prim.IsLoaded():
            raise RuntimeError(
                f"Moved payload not loaded: "
                f"{new_payload}"
            )


def verify_unrelated_loaded_paths(
    stage,
    before_load_set,
    moves,
):
    """
    Verify unrelated payload working-set state remains intact.

    Old paths under moved roots are excluded because those are expected to move.
    """
    expected_unrelated = {
        path
        for path in before_load_set
        if not any(
            is_at_or_below(
                path,
                old_root,
            )
            for old_root, _ in moves
        )
    }

    after = set(
        stage.GetLoadSet()
    )

    missing = (
        expected_unrelated
        - after
    )

    if missing:
        print_paths(
            "Missing unrelated loaded paths:",
            missing,
        )

        raise RuntimeError(
            "Unrelated loaded paths changed"
        )


# ============================================================================
# Optional comparison: legacy full SetLoadRules method
# ============================================================================

def remap_all_load_rules(
    rules,
    moves,
):
    out = Usd.StageLoadRules()

    for path, policy in rules.GetRules():
        mapped = path

        for old_root, new_root in moves:
            mapped = remap_path(
                mapped,
                old_root,
                new_root,
            )

        out.AddRule(
            mapped,
            policy,
        )

    return out


def run_legacy_comparison(
    main_filename,
    moves,
):
    """
    Optional old-method benchmark.

    This is intentionally run on a separate reopened stage so the targeted
    method and legacy SetLoadRules method do not influence each other.
    """
    print("")
    print("")
    print("=" * 78)
    print("LEGACY COMPARISON: FULL SetLoadRules")
    print("=" * 78)

    stage, _ = open_stageviz_style_stage(
        main_filename
    )

    validate_move_fixture(
        stage,
        moves,
    )

    original_rules = (
        stage.GetLoadRules()
    )

    timings = []

    for index, (
        old_path,
        new_path,
    ) in enumerate(moves):

        timings.append(
            namespace_move_timed(
                stage,
                old_path,
                new_path,
                f"legacy move {index}",
            )
        )

    started = time.perf_counter()

    remapped_rules = remap_all_load_rules(
        original_rules,
        moves,
    )

    rule_remap_seconds = (
        time.perf_counter()
        - started
    )

    print("")
    print(
        "Legacy full rule remap:",
        f"{ms(rule_remap_seconds):.2f} ms",
    )

    print(
        "Legacy SetLoadRules rules:",
        len(
            remapped_rules.GetRules()
        ),
    )

    started = time.perf_counter()

    stage.SetLoadRules(
        remapped_rules
    )

    set_rules_seconds = (
        time.perf_counter()
        - started
    )

    print(
        "Legacy SetLoadRules:",
        f"{ms(set_rules_seconds):.2f} ms",
    )

    expected_payloads = (
        expected_moved_payload_paths(
            moves
        )
    )

    verify_namespace_result(
        stage,
        moves,
    )

    verify_moved_payload_loads(
        stage,
        expected_payloads,
    )

    namespace_total = sum(
        item["total"]
        for item in timings
    )

    return {
        "namespace": namespace_total,
        "rule_remap": rule_remap_seconds,
        "load_restore": set_rules_seconds,
        "total": (
            namespace_total
            + rule_remap_seconds
            + set_rules_seconds
        ),
    }


# ============================================================================
# Main targeted StageViz-equivalent test
# ============================================================================

run_id = uuid.uuid4().hex[:8]

temp_dir = os.path.join(
    tempfile.gettempdir(),
    "usd_25k_stageviz_namespace_load_test",
)

os.makedirs(
    temp_dir,
    exist_ok=True,
)

payload_filename = os.path.join(
    temp_dir,
    f"payload_{run_id}.usda",
)

targeted_main_filename = os.path.join(
    temp_dir,
    f"main_targeted_{run_id}.usda",
)

legacy_main_filename = os.path.join(
    temp_dir,
    f"main_legacy_{run_id}.usda",
)


print("")
print("=" * 78)
print("CREATE 25K STAGEVIZ NAMESPACE / PAYLOAD TEST")
print("=" * 78)
print("")

started = time.perf_counter()

payload_stage = make_payload_file(
    payload_filename
)

targeted_stage, xform_count, payload_count = (
    make_main_stage(
        targeted_main_filename,
        payload_filename,
    )
)

legacy_stage, legacy_xform_count, legacy_payload_count = (
    make_main_stage(
        legacy_main_filename,
        payload_filename,
    )
)

if legacy_xform_count != xform_count:
    raise RuntimeError(
        "Targeted/legacy fixture Xform counts differ"
    )

if legacy_payload_count != payload_count:
    raise RuntimeError(
        "Targeted/legacy fixture payload counts differ"
    )

fixture_seconds = (
    time.perf_counter()
    - started
)

print(
    "Main-layer Xforms:",
    f"{xform_count:,}",
)

print(
    "Payload arcs:",
    f"{payload_count:,}",
)

print(
    "Fixture creation:",
    f"{ms(fixture_seconds):.2f} ms",
)

# Drop authoring stage references before reopening.
payload_stage = None
targeted_stage = None
legacy_stage = None


print("")
print("")
print("=" * 78)
print("TARGETED METHOD: STAGEVIZ-EQUIVALENT MOVE")
print("=" * 78)

stage, explicit_loaded = (
    open_stageviz_style_stage(
        targeted_main_filename
    )
)

validate_move_fixture(
    stage,
    MOVE_SPECS,
)

before_load_set = set(
    stage.GetLoadSet()
)

print_paths(
    "Load-set paths under moved roots before move:",
    {
        path
        for path in before_load_set
        if any(
            is_at_or_below(
                path,
                old_root,
            )
            for old_root, _ in MOVE_SPECS
        )
    },
)

capture_started = time.perf_counter()

affected_loaded = (
    collect_loaded_paths_under_moves(
        stage,
        MOVE_SPECS,
    )
)

capture_seconds = (
    time.perf_counter()
    - capture_started
)

affected_count = sum(
    len(item[2])
    for item in affected_loaded
)

print("")
print(
    "Affected loaded paths captured:",
    affected_count,
)

print(
    "Capture time:",
    f"{ms(capture_seconds):.2f} ms",
)

move_timings = []

for index, (
    old_path,
    new_path,
) in enumerate(MOVE_SPECS):

    move_timings.append(
        namespace_move_timed(
            stage,
            old_path,
            new_path,
            f"targeted move {index}",
        )
    )

remap_started = time.perf_counter()

remapped_loaded_paths = (
    build_remapped_loaded_paths(
        affected_loaded
    )
)

remap_seconds = (
    time.perf_counter()
    - remap_started
)

print_paths(
    "Targeted paths to reload:",
    remapped_loaded_paths,
)

print(
    "Loaded-path remap:",
    f"{ms(remap_seconds):.2f} ms",
)

load_restore_seconds = (
    restore_targeted_loads(
        stage,
        remapped_loaded_paths,
    )
)

print("")
print(
    "Targeted LoadAndUnload:",
    f"{ms(load_restore_seconds):.2f} ms",
)

verify_namespace_result(
    stage,
    MOVE_SPECS,
)

expected_payloads = (
    expected_moved_payload_paths(
        MOVE_SPECS
    )
)

verify_moved_payload_loads(
    stage,
    expected_payloads,
)

verify_unrelated_loaded_paths(
    stage,
    before_load_set,
    MOVE_SPECS,
)

print("")
print("TARGETED METHOD CORRECTNESS: PASS")

targeted_namespace_total = sum(
    item["total"]
    for item in move_timings
)

targeted_total = (
    capture_seconds
    + targeted_namespace_total
    + remap_seconds
    + load_restore_seconds
)


# ============================================================================
# Legacy comparison
# ============================================================================

legacy = run_legacy_comparison(
    legacy_main_filename,
    MOVE_SPECS,
)


# ============================================================================
# Summary
# ============================================================================

print("")
print("")
print("=" * 78)
print("FINAL BENCHMARK SUMMARY")
print("=" * 78)
print("")

print("FIXTURE")
print(
    "  Main Xforms:",
    f"{xform_count:10,d}",
)
print(
    "  Payload arcs:",
    f"{payload_count:10,d}",
)
print(
    "  Initially loaded:",
    f"{len(explicit_loaded):10,d}",
)

print("")
print("TARGETED METHOD")
print(
    "  Capture loaded paths:",
    f"{ms(capture_seconds):10.2f} ms",
)
print(
    "  Namespace edits:",
    f"{ms(targeted_namespace_total):10.2f} ms",
)
print(
    "  Remap loaded paths:",
    f"{ms(remap_seconds):10.2f} ms",
)
print(
    "  LoadAndUnload:",
    f"{ms(load_restore_seconds):10.2f} ms",
)
print(
    "  Total:",
    f"{ms(targeted_total):10.2f} ms",
)

print("")
print("LEGACY METHOD")
print(
    "  Namespace edits:",
    f"{ms(legacy['namespace']):10.2f} ms",
)
print(
    "  Remap all rules:",
    f"{ms(legacy['rule_remap']):10.2f} ms",
)
print(
    "  SetLoadRules:",
    f"{ms(legacy['load_restore']):10.2f} ms",
)
print(
    "  Total:",
    f"{ms(legacy['total']):10.2f} ms",
)

print("")

if targeted_total > 0.0:
    print(
        "Overall speedup:",
        f"{legacy['total'] / targeted_total:.2f}x",
    )

if load_restore_seconds > 0.0:
    print(
        "Load-state update speedup:",
        f"{legacy['load_restore'] / load_restore_seconds:.2f}x",
    )

print("")
print("Correctness:")
print("  Targeted method: PASS")
print("  Legacy method:   PASS")

print("")
print("Files:")
print("  targeted:", targeted_main_filename)
print("  legacy:  ", legacy_main_filename)
print("  payload: ", payload_filename)
