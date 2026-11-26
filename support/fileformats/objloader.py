# SPDX-License-Identifier: BSD-3-Clause
"""Small Wavefront OBJ polygon importer; no third-party conversion dependency."""
import math
from pathlib import Path
import re
import warnings

from pxr import Gf, Sdf, Usd, UsdGeom
from .objutils import triangulate


def _index(value, count):
    index = int(value)
    result = index - 1 if index > 0 else count + index
    if index == 0 or not 0 <= result < count:
        raise ValueError('OBJ index out of range: ' + value)
    return result


def _vector(values, minimum):
    numbers = [float(v) for v in values]
    if len(numbers) < minimum or not all(math.isfinite(v) for v in numbers):
        raise ValueError('Invalid OBJ vector')
    return numbers


def load(source, options):
    """Return an in-memory USD stage, splitting OBJ object/group face batches."""
    points, uv, normals, groups = [], [], [], []
    faces, name = [], 'Mesh'
    materials = False
    pending = ''
    for line_number, line in enumerate(Path(source).read_text(encoding='utf-8-sig').splitlines(), 1):
        line = pending + line.split('#', 1)[0].strip()
        if line.endswith('\\'):
            pending = line[:-1] + ' '
            continue
        pending = ''
        fields = line.split()
        if not fields:
            continue
        kind, values = fields[0], fields[1:]
        try:
            if kind == 'v':
                v = _vector(values, 3)
                weight = v[3] if len(v) == 4 else 1.0
                if weight == 0:
                    raise ValueError('Zero homogeneous vertex weight')
                points.append(tuple(component / weight for component in v[:3]))
            elif kind == 'vt':
                v = _vector(values, 1)
                uv.append((v[0], v[1] if len(v) > 1 else 0.0))
            elif kind == 'vn':
                normals.append(tuple(_vector(values, 3)[:3]))
            elif kind in ('o', 'g'):
                if faces:
                    groups.append((name, faces))
                    faces = []
                name = '_'.join(values) or 'Mesh'
            elif kind == 'f':
                if len(values) < 3:
                    raise ValueError('Faces need at least three vertices')
                face = []
                for value in values:
                    indices = value.split('/')
                    if len(indices) > 3:
                        raise ValueError('Invalid face corner')
                    indices += [''] * (3 - len(indices))
                    face.append((_index(indices[0], len(points)),
                                 _index(indices[1], len(uv)) if indices[1] else None,
                                 _index(indices[2], len(normals)) if indices[2] else None))
                faces.extend(triangulate(face, points) if options.get('triangulate', False) else [face])
            elif kind in ('mtllib', 'usemtl'):
                materials = True
            elif kind != 's':
                raise ValueError('Unsupported OBJ statement: ' + kind)
        except (ValueError, IndexError) as error:
            raise ValueError(f'{Path(source).name}:{line_number}: {error}') from error
    if pending:
        raise ValueError('Unterminated OBJ line continuation')
    if faces:
        groups.append((name, faces))
    if not groups:
        raise ValueError('OBJ contains no polygon faces')
    if materials:
        warnings.warn('OBJ sample imports geometry only; material libraries are ignored', stacklevel=2)

    stage = Usd.Stage.CreateInMemory()
    world = UsdGeom.Xform.Define(stage, '/World')
    stage.SetDefaultPrim(world.GetPrim())
    UsdGeom.SetStageUpAxis(stage, UsdGeom.Tokens.y)
    UsdGeom.SetStageMetersPerUnit(stage, 1.0)
    used = set()
    for name, faces in groups:
        name = re.sub(r'[^a-zA-Z0-9_]', '_', name)
        if not name or name[0].isdigit():
            name = 'Mesh_' + name
        base, number = name, 1
        while name in used:
            name = f'{base}_{number}'
            number += 1
        used.add(name)
        mesh = UsdGeom.Mesh.Define(stage, '/World/' + name)
        remap = {}
        corners = [corner for face in faces for corner in face]
        for vertex, _, _ in corners:
            if vertex not in remap:
                remap[vertex] = len(remap)
        mesh.CreatePointsAttr([Gf.Vec3f(*points[i]) for i in remap])
        mesh.CreateFaceVertexCountsAttr([len(face) for face in faces])
        mesh.CreateFaceVertexIndicesAttr([remap[corner[0]] for corner in corners])
        mesh.CreateSubdivisionSchemeAttr(UsdGeom.Tokens.none)
        for channel, data in ((1, uv), (2, normals)):
            if all(corner[channel] is not None for corner in corners):
                values = [data[corner[channel]] for corner in corners]
                if channel == 1:
                    UsdGeom.PrimvarsAPI(mesh).CreatePrimvar('st', Sdf.ValueTypeNames.TexCoord2fArray,
                                                          UsdGeom.Tokens.faceVarying).Set(values)
                else:
                    mesh.CreateNormalsAttr(values)
                    mesh.SetNormalsInterpolation(UsdGeom.Tokens.faceVarying)
            elif any(corner[channel] is not None for corner in corners):
                warnings.warn('OBJ sample omits partially specified UVs/normals on ' + name, stacklevel=2)
        mesh.CreateExtentAttr(UsdGeom.PointBased(mesh).ComputeExtent(mesh.GetPointsAttr().Get()))
    return stage
