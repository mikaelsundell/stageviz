# SPDX-License-Identifier: BSD-3-Clause
"""Generate independent 80-quad mesh prims for the interaction benchmark."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import time
from pxr import Gf, Sdf, Vt


def generate(destination, meshes=250000):
    destination = Path(destination).resolve()
    destination.parent.mkdir(parents=True, exist_ok=True)
    started = time.perf_counter()
    layer = Sdf.Layer.CreateNew(str(destination))
    layer.defaultPrim = 'World'
    layer.pseudoRoot.SetInfo('upAxis', 'Y')
    layer.pseudoRoot.SetInfo('metersPerUnit', 1.0)
    points = Vt.Vec3fArray([Gf.Vec3f(x / 8 - .5, .06 * math.sin(x + z), z / 10 - .5)
                          for z in range(11) for x in range(9)])
    counts = Vt.IntArray([4] * 80)
    indices = Vt.IntArray([v for z in range(10) for x in range(8)
                           for v in (z * 9 + x, z * 9 + x + 1, (z + 1) * 9 + x + 1, (z + 1) * 9 + x)])
    extent = Vt.Vec3fArray([Gf.Vec3f(-.5, -.06, -.5), Gf.Vec3f(.5, .06, .5)])

    def attribute(prim, name, kind, value, uniform=False):
        spec = Sdf.AttributeSpec(prim, name, kind,
                                 Sdf.VariabilityUniform if uniform else Sdf.VariabilityVarying, False)
        spec.default = value

    columns = math.ceil(math.sqrt(meshes))
    groups = math.ceil(meshes / 1000)
    with Sdf.ChangeBlock():
        world = Sdf.PrimSpec(layer, 'World', Sdf.SpecifierDef, 'Xform')
        for group_index in range(groups):
            group = Sdf.PrimSpec(world, 'Batch_%03d' % group_index, Sdf.SpecifierDef, 'Xform')
            first = group_index * 1000
            last = min(meshes, (group_index + 1) * 1000) - 1
            center = Gf.Vec3d((columns - 1) * .625, 0, (first // columns + last // columns) * .625)
            attribute(group, 'xformOp:transform', Sdf.ValueTypeNames.Matrix4d, Gf.Matrix4d(1).SetTranslate(center))
            attribute(group, 'xformOpOrder', Sdf.ValueTypeNames.TokenArray, Vt.TokenArray(['xformOp:transform']), True)
            for i in range(group_index * 1000, min(meshes, (group_index + 1) * 1000)):
                prim = Sdf.PrimSpec(group, 'Mesh_%06d' % i, Sdf.SpecifierDef, 'Mesh')
                attribute(prim, 'points', Sdf.ValueTypeNames.Point3fArray, points)
                attribute(prim, 'faceVertexCounts', Sdf.ValueTypeNames.IntArray, counts)
                attribute(prim, 'faceVertexIndices', Sdf.ValueTypeNames.IntArray, indices)
                attribute(prim, 'extent', Sdf.ValueTypeNames.Float3Array, extent)
                attribute(prim, 'subdivisionScheme', Sdf.ValueTypeNames.Token, 'none', True)
                attribute(prim, 'doubleSided', Sdf.ValueTypeNames.Bool, True, True)
                matrix = Gf.Matrix4d(1).SetTranslate(Gf.Vec3d((i % columns) * 1.25, 0, (i // columns) * 1.25) - center)
                attribute(prim, 'xformOp:transform', Sdf.ValueTypeNames.Matrix4d, matrix)
                attribute(prim, 'xformOpOrder', Sdf.ValueTypeNames.TokenArray, Vt.TokenArray(['xformOp:transform']), True)
            if group_index % 25 == 0:
                print('Generated', min(meshes, (group_index + 1) * 1000), '/', meshes, flush=True)
    layer.Save()
    manifest = dict(meshes=meshes, groups=groups, stage_prims=meshes + groups + 1,
                    faces_per_mesh=80, faces=meshes * 80, points_per_mesh=99,
                    geometry='independent non-instanced quad meshes; subdivision disabled',
                    file=str(destination), bytes=destination.stat().st_size,
                    sha256=hashlib.sha256(destination.read_bytes()).hexdigest(),
                    generation_seconds=time.perf_counter() - started)
    destination.with_suffix('.json').write_text(json.dumps(manifest, indent=2))
    print(json.dumps(manifest, indent=2), flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('destination')
    parser.add_argument('--meshes', type=int, default=250000)
    args = parser.parse_args()
    generate(args.destination, args.meshes)
