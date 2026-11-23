# SPDX-License-Identifier: BSD-3-Clause
"""Generate a deterministic hierarchy with independent, mixed geometry."""

PARTS_PER_ASSEMBLY = 100
ASSEMBLIES_PER_SECTION = 20
SELECTION_BATCHES = 4
SELECTION_SIZE = 96

# Each topology is composed independently. Include both flat and volumetric
# polygon meshes, with and without subdivision.
MESHES = (
    ('(-.4, 0, -.4), (.4, 0, -.4), (0, .4, .4)', '3', '0, 2, 1', 'none'),
    ('(-.4, 0, -.4), (.4, 0, -.4), (.4, 0, .4), (-.4, 0, .4)', '4', '0, 3, 2, 1', 'none'),
    ('(-.4, 0, -.4), (.4, 0, -.4), (.4, 0, .4), (-.4, 0, .4), (0, .5, 0)',
     '4, 3, 3, 3, 3', '0, 1, 2, 3, 0, 4, 1, 1, 4, 2, 2, 4, 3, 3, 4, 0', 'none'),
    ('(-.35, 0, -.35), (.35, 0, -.35), (.35, 0, .35), (-.35, 0, .35),'
     ' (-.35, .7, -.35), (.35, .7, -.35), (.35, .7, .35), (-.35, .7, .35)',
     '4, 4, 4, 4, 4, 4', '0, 1, 2, 3, 4, 7, 6, 5, 0, 4, 5, 1, 1, 5, 6, 2, 2, 6, 7, 3, 3, 7, 4, 0',
     'catmullClark'),
)
DETAILS = (
    ('Sphere', 'double radius = 0.18'),
    ('Cube', 'double size = 0.35'),
    ('Cone', 'double radius = 0.18\ndouble height = 0.5\nuniform token axis = "Y"'),
    ('Cylinder', 'double radius = 0.18\ndouble height = 0.5\nuniform token axis = "Y"'),
    ('Capsule', 'double radius = 0.14\ndouble height = 0.35\nuniform token axis = "Y"'),
)


def component_path(index):
    assembly = index // PARTS_PER_ASSEMBLY
    section = assembly // ASSEMBLIES_PER_SECTION
    return f'/World/Section_{section:03d}/Assembly_{assembly:04d}/Component_{index:06d}'


def selection_batch(parts, batch):
    candidates = range(batch % SELECTION_BATCHES, parts, SELECTION_BATCHES)
    count = min(SELECTION_SIZE, len(candidates))
    suffixes = ('', '/Model', '/Model/Primary', '/Model/Detail')
    return [
        component_path(candidates[position * (len(candidates) - 1) // max(1, count - 1)])
        + suffixes[position % len(suffixes)]
        for position in range(count)
    ]


def write_hierarchy_fixture(path, parts):
    assemblies = (parts + PARTS_PER_ASSEMBLY - 1) // PARTS_PER_ASSEMBLY
    sections = (assemblies + ASSEMBLIES_PER_SECTION - 1) // ASSEMBLIES_PER_SECTION
    width = max(1, int(parts ** 0.5))
    with path.open('w', encoding='utf-8', newline='\n') as stream:
        stream.write('#usda 1.0\n(defaultPrim = "World"\nupAxis = "Y"\nmetersPerUnit = 1)\n'
                     'def Xform "World" {\n')
        for section in range(sections):
            stream.write(f'def Xform "Section_{section:03d}" {{\n')
            first_assembly = section * ASSEMBLIES_PER_SECTION
            for assembly in range(first_assembly, min(first_assembly + ASSEMBLIES_PER_SECTION, assemblies)):
                stream.write(f'def Xform "Assembly_{assembly:04d}" {{\n')
                first_part = assembly * PARTS_PER_ASSEMBLY
                for index in range(first_part, min(first_part + PARTS_PER_ASSEMBLY, parts)):
                    points, counts, indices, subdivision = MESHES[index % len(MESHES)]
                    detail, attributes = DETAILS[index % len(DETAILS)]
                    stream.write(
                        f'def Xform "Component_{index:06d}" {{\n'
                        f'double3 xformOp:translate = ({index % width}, 0, {index // width})\n'
                        'uniform token[] xformOpOrder = ["xformOp:translate"]\n'
                        'def Xform "Model" {\n'
                        'def Mesh "Primary" {\n'
                        f'point3f[] points = [{points}]\n'
                        f'int[] faceVertexCounts = [{counts}]\n'
                        f'int[] faceVertexIndices = [{indices}]\n'
                        f'uniform token subdivisionScheme = "{subdivision}"\n'
                        'uniform bool doubleSided = true\n}\n'
                        f'def {detail} "Detail" {{\n{attributes}\n'
                        'double3 xformOp:translate = (0, 0.55, 0)\n'
                        'uniform token[] xformOpOrder = ["xformOp:translate"]\n}\n}\n'
                        'def Scope "Metadata" {\ndef Xform "Locator" {}\n}\n}\n'
                    )
                stream.write('}\n')
            stream.write('}\n')
        stream.write('}\n')
    return 1 + sections + assemblies + 6 * parts
