from pxr import Sdf, Gf, Vt
import stageviz
import time

session = stageviz.session()
stage = session.stage()

if not stage:
    raise RuntimeError("No stage loaded")

root_path = "/StageTreeStress"

# ----------------------------------------------------------------------
# Configuration
# ----------------------------------------------------------------------

XFORM_TARGET = 25000
GPRIM_TARGET = 5000

ASSEMBLY_COUNT = 100
GROUPS_PER_ASSEMBLY = 50

# 100 * 50 = 5,000 groups / cubes
assert ASSEMBLY_COUNT * GROUPS_PER_ASSEMBLY == GPRIM_TARGET


# ----------------------------------------------------------------------
# Helpers
# ----------------------------------------------------------------------

def create_prim(layer, path, type_name):
    prim = Sdf.CreatePrimInLayer(
        layer,
        path,
    )

    prim.specifier = Sdf.SpecifierDef
    prim.typeName = type_name

    return prim


def create_attribute(
    prim,
    name,
    type_name,
    value,
    variability=Sdf.VariabilityVarying,
):
    attr = Sdf.AttributeSpec(
        prim,
        name,
        type_name,
        variability,
    )

    attr.default = value
    return attr


def set_translate(prim, value):
    create_attribute(
        prim,
        "xformOp:translate",
        Sdf.ValueTypeNames.Double3,
        Gf.Vec3d(*value),
    )

    create_attribute(
        prim,
        "xformOpOrder",
        Sdf.ValueTypeNames.TokenArray,
        Vt.TokenArray([
            "xformOp:translate",
        ]),
        Sdf.VariabilityUniform,
    )


def set_trs(
    prim,
    translate,
    rotate,
    scale,
):
    create_attribute(
        prim,
        "xformOp:translate",
        Sdf.ValueTypeNames.Double3,
        Gf.Vec3d(*translate),
    )

    create_attribute(
        prim,
        "xformOp:rotateXYZ",
        Sdf.ValueTypeNames.Float3,
        Gf.Vec3f(*rotate),
    )

    create_attribute(
        prim,
        "xformOp:scale",
        Sdf.ValueTypeNames.Float3,
        Gf.Vec3f(*scale),
    )

    create_attribute(
        prim,
        "xformOpOrder",
        Sdf.ValueTypeNames.TokenArray,
        Vt.TokenArray([
            "xformOp:translate",
            "xformOp:rotateXYZ",
            "xformOp:scale",
        ]),
        Sdf.VariabilityUniform,
    )


def create_cube(
    layer,
    path,
    size,
    translate,
    rotate,
    scale,
    color,
):
    prim = create_prim(
        layer,
        path,
        "Cube",
    )

    create_attribute(
        prim,
        "size",
        Sdf.ValueTypeNames.Double,
        float(size),
    )

    create_attribute(
        prim,
        "extent",
        Sdf.ValueTypeNames.Float3Array,
        Vt.Vec3fArray([
            Gf.Vec3f(
                -size * 0.5,
                -size * 0.5,
                -size * 0.5,
            ),
            Gf.Vec3f(
                size * 0.5,
                size * 0.5,
                size * 0.5,
            ),
        ]),
    )

    create_attribute(
        prim,
        "primvars:displayColor",
        Sdf.ValueTypeNames.Color3fArray,
        Vt.Vec3fArray([
            Gf.Vec3f(*color),
        ]),
    )

    create_attribute(
        prim,
        "primvars:displayColor:interpolation",
        Sdf.ValueTypeNames.Token,
        "constant",
        Sdf.VariabilityUniform,
    )

    set_trs(
        prim,
        translate,
        rotate,
        scale,
    )

    return prim


# ----------------------------------------------------------------------
# Edit layer
# ----------------------------------------------------------------------

layer = stage.GetEditTarget().GetLayer()

if not layer:
    raise RuntimeError("No editable USD layer")

print("")
print("=" * 72)
print("STAGETREE 30K STRESS SCENE")
print("=" * 72)
print("")
print("Target:")
print(f"  Xforms : {XFORM_TARGET:,}")
print(f"  Gprims : {GPRIM_TARGET:,}")
print(f"  Total  : {XFORM_TARGET + GPRIM_TARGET:,}")
print("")
print("Authoring into:")
print(" ", layer.identifier)
print("")

started = time.perf_counter()


# ----------------------------------------------------------------------
# Remove previous test
# ----------------------------------------------------------------------

existing = layer.GetPrimAtPath(
    Sdf.Path(root_path)
)

if existing:
    layer.RemovePrimIfInert(
        Sdf.Path(root_path)
    )

    # RemovePrimIfInert only works for inert specs.
    # Force removal if this is a previous populated test hierarchy.
    if layer.GetPrimAtPath(Sdf.Path(root_path)):
        parent = layer.pseudoRoot
        for child in list(parent.nameChildren):
            if child.name == "StageTreeStress":
                del parent.nameChildren[
                    "StageTreeStress"
                ]
                break


# ----------------------------------------------------------------------
# Build
# ----------------------------------------------------------------------

xform_count = 0
gprim_count = 0

with Sdf.ChangeBlock():

    root = create_prim(
        layer,
        root_path,
        "Xform",
    )

    xform_count += 1

    #
    # 100 assemblies
    #
    # Each assembly contains:
    #
    #   Assembly_###
    #       Group_##
    #           Box
    #           Node_00
    #           Node_01
    #           Node_02
    #           ...
    #
    # We first create all assembly/group xforms and cubes.
    # Then additional nested nodes are added until exactly 25k
    # Xforms have been authored.
    #

    group_paths = []

    for assembly_index in range(
        ASSEMBLY_COUNT
    ):
        assembly_path = (
            f"{root_path}/"
            f"Assembly_{assembly_index:03d}"
        )

        assembly = create_prim(
            layer,
            assembly_path,
            "Xform",
        )

        xform_count += 1

        assembly_x = (
            assembly_index % 10
        ) * 600.0

        assembly_z = (
            assembly_index // 10
        ) * 600.0

        set_translate(
            assembly,
            (
                assembly_x,
                0.0,
                assembly_z,
            ),
        )

        for group_index in range(
            GROUPS_PER_ASSEMBLY
        ):
            group_path = (
                f"{assembly_path}/"
                f"Group_{group_index:02d}"
            )

            group = create_prim(
                layer,
                group_path,
                "Xform",
            )

            xform_count += 1
            group_paths.append(group_path)

            gx = (
                group_index % 10
            ) * 45.0

            gy = (
                group_index // 10
            ) * 45.0

            gz = (
                (group_index % 3) - 1
            ) * 12.0

            set_trs(
                group,
                (
                    gx,
                    gy,
                    gz,
                ),
                (
                    float(
                        (assembly_index * 3)
                        % 17
                    ),
                    float(
                        (group_index * 7)
                        % 23
                    ),
                    float(
                        (
                            assembly_index
                            + group_index
                        )
                        % 13
                    ),
                ),
                (
                    1.0,
                    1.0,
                    1.0,
                ),
            )

            #
            # One rendered Gprim per group.
            #

            box_index = gprim_count

            size = (
                12.0
                + float(box_index % 9)
            )

            sx = (
                0.7
                + (
                    (box_index % 5)
                    * 0.12
                )
            )

            sy = (
                0.75
                + (
                    (box_index % 7)
                    * 0.08
                )
            )

            sz = (
                0.8
                + (
                    (box_index % 3)
                    * 0.18
                )
            )

            color_mode = (
                box_index % 6
            )

            colors = (
                (0.85, 0.18, 0.12),
                (0.16, 0.55, 0.90),
                (0.18, 0.78, 0.32),
                (0.88, 0.64, 0.12),
                (0.62, 0.24, 0.82),
                (0.20, 0.72, 0.72),
            )

            create_cube(
                layer,
                f"{group_path}/Box",
                size,
                (
                    float(
                        (box_index % 4) * 3
                    ),
                    float(
                        (box_index % 5) * 2
                    ),
                    float(
                        (box_index % 6) * 2
                    ),
                ),
                (
                    float(
                        (box_index * 3)
                        % 18
                    ),
                    float(
                        (box_index * 5)
                        % 27
                    ),
                    float(
                        (box_index * 7)
                        % 31
                    ),
                ),
                (
                    sx,
                    sy,
                    sz,
                ),
                colors[color_mode],
            )

            gprim_count += 1


    # ------------------------------------------------------------------
    # Add additional Xforms until we reach exactly 25,000.
    #
    # They are distributed across the 5,000 groups and arranged in
    # short nested chains. This gives StageTree both breadth and depth.
    # ------------------------------------------------------------------

    remaining = (
        XFORM_TARGET
        - xform_count
    )

    print(
        "Base Xforms:",
        f"{xform_count:,}",
    )

    print(
        "Additional Xforms:",
        f"{remaining:,}",
    )

    node_index = 0

    while xform_count < XFORM_TARGET:

        group_index = (
            node_index
            % len(group_paths)
        )

        group_path = (
            group_paths[group_index]
        )

        chain_index = (
            node_index
            // len(group_paths)
        )

        #
        # Alternate between a shallow and nested hierarchy.
        #

        if chain_index == 0:
            node_path = (
                f"{group_path}/"
                f"Node_{chain_index:02d}"
            )

        else:
            parent = (
                f"{group_path}/"
                "Node_00"
            )

            for depth in range(
                1,
                chain_index + 1
            ):
                parent += (
                    f"/Node_{depth:02d}"
                )

            node_path = parent

        node = create_prim(
            layer,
            node_path,
            "Xform",
        )

        set_translate(
            node,
            (
                float(
                    (node_index % 5)
                    * 3
                ),
                float(
                    (node_index % 7)
                    * 2
                ),
                float(
                    (node_index % 11)
                    * 1.5
                ),
            ),
        )

        xform_count += 1
        node_index += 1


# ----------------------------------------------------------------------
# Finished
# ----------------------------------------------------------------------

elapsed = (
    time.perf_counter()
    - started
)

print("")
print("=" * 72)
print("CREATED")
print("=" * 72)

print(
    "Root:",
    root_path,
)

print(
    "Xforms:",
    f"{xform_count:,}",
)

print(
    "Gprims:",
    f"{gprim_count:,}",
)

print(
    "Total:",
    f"{xform_count + gprim_count:,}",
)

print(
    "Authoring time:",
    f"{elapsed:.3f} s",
)

print("")
print("Useful StageTree test paths:")
print(
    f"  {root_path}/Assembly_000"
)
print(
    f"  {root_path}/Assembly_000/Group_00"
)
print(
    f"  {root_path}/Assembly_000/Group_00/Box"
)
print(
    f"  {root_path}/Assembly_050/Group_25"
)
print(
    f"  {root_path}/Assembly_099/Group_49"
)

print("")
print("Good move test:")
print(
    f"  source:      "
    f"{root_path}/Assembly_000/Group_00"
)
print(
    f"  destination: "
    f"{root_path}/Assembly_099"
)

print("")
print("Good rename test:")
print(
    f"  {root_path}/Assembly_050/Group_25"
)