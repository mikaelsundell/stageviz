# SPDX-License-Identifier: BSD-3-Clause
"""Embedded bounds checks; external run_bounds.py owns MCP and the process."""
import json
from pathlib import Path
import random
import statistics
import time
import stageviz
from pxr import Gf, Usd, UsdGeom

session = stageviz.session()
stage = session.stage()
root, branch = CONFIG['root'], CONFIG['branch']
assert stage.GetPrimAtPath(root) and stage.GetPrimAtPath(branch)
meshes = [str(p.GetPath()) for p in Usd.PrimRange(stage.GetPrimAtPath(root)) if p.IsA(UsdGeom.Mesh)]
random.Random(20261106).shuffle(meshes)
cases = [('branch', [branch]), ('root', [root]),
         ('mesh_sample', meshes[:10000]), ('overlap', [root, branch] + meshes[:10])]


def reference(paths):
    # Deliberately reproduce the old framing policy rather than substituting
    # one ancestor bound, which would change invisible descendant behavior.
    cache = UsdGeom.BBoxCache(Usd.TimeCode.Default(), ['default', 'proxy', 'render'], True)
    result, visited = Gf.Range3d(), set()
    for path in paths:
        for prim in Usd.PrimRange(stage.GetPrimAtPath(path)):
            if not prim.IsActive() or not prim.IsDefined() or prim.GetPath() in visited:
                continue
            visited.add(prim.GetPath())
            if UsdGeom.Imageable(prim):
                result.UnionWith(cache.ComputeWorldBound(prim).ComputeAlignedRange())
    return tuple(result.GetMin()), tuple(result.GetMax())


def same(actual, expected):
    return all(Gf.IsClose(Gf.Vec3d(a), Gf.Vec3d(b), 1e-6) for a, b in zip(actual, expected))


results = []
for name, paths in cases:
    times, bounds = [], None
    for repeat in range(CONFIG['repeats'] + 1):
        start = time.perf_counter()
        value = session.boundingBox(paths)
        times.append((time.perf_counter() - start) * 1000)
        if bounds is None:
            bounds = value
        assert same(value, bounds), (name, repeat, 'warm bounds changed')
    assert same(bounds, reference(paths)), (name, 'fresh USD reference differs')
    results.append(dict(case=name, paths=len(paths), first_ms=times[0],
                        warm_ms=times[1:], warm_median_ms=statistics.median(times[1:]), bounds=bounds))

paths = [branch]
before = session.boundingBox(paths)
world = UsdGeom.XformCache().GetLocalToWorldTransform(stage.GetPrimAtPath(branch))
after = world * Gf.Matrix4d(1).SetTranslate(Gf.Vec3d(13, 17, 19))
matrix = lambda value: [list(row) for row in value]
stageviz.command.set_transforms(paths, [matrix(world)], [matrix(after)])
assert same(session.boundingBox(paths), reference(paths)), 'edit invalidates selection cache synchronously'
assert not same(session.boundingBox(paths), before), 'transform changes the requested bounds'
stageviz.command.undo()
assert same(session.boundingBox(paths), before), 'undo invalidates selection cache synchronously'
stageviz.command.redo()
assert same(session.boundingBox(paths), reference(paths)), 'redo invalidates selection cache synchronously'
stageviz.command.undo()
assert same(session.boundingBox(paths), before)
(Path(CONFIG['output'])/'bounds.json').write_text(json.dumps(dict(cases=results, mutation_undo_redo=True), indent=2))
print('PASS selection bounds, fresh reference, synchronous invalidation and undo/redo')
