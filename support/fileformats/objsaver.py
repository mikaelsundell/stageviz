# SPDX-License-Identifier: BSD-3-Clause
"""Export USD polygon meshes to one OBJ file, baking world transforms."""
import math
from pathlib import Path

from pxr import Gf, Usd, UsdGeom
from .objutils import triangulate


def _channel(values, interpolation, face, vertex, corner):
    index = {'constant': 0, 'uniform': face, 'vertex': vertex,
             'varying': vertex, 'faceVarying': corner}.get(str(interpolation))
    if index is None or not 0 <= index < len(values):
        raise ValueError('Invalid USD normals/UV interpolation or array length')
    return values[index]


def _numbers(values):
    if not all(math.isfinite(float(value)) for value in values):
        raise ValueError('OBJ cannot represent non-finite coordinates')
    return ' '.join(format(float(value), '.9g') for value in values)


def save(stage, destination, options):
    """Export visible mesh control cages at default time, including instances."""
    if not stage:
        raise ValueError('A valid USD stage is required')
    cache = UsdGeom.XformCache(Usd.TimeCode.Default())
    vertex_offset = uv_offset = normal_offset = 0
    mesh_count = 0
    with Path(destination).open('w', encoding='utf-8', newline='\n') as stream:
        stream.write('# Wavefront OBJ exported by Stageviz sample plugin\n')
        for prim in Usd.PrimRange.Stage(stage, Usd.TraverseInstanceProxies()):
            imageable = UsdGeom.Imageable(prim)
            if imageable and imageable.ComputeVisibility() == UsdGeom.Tokens.invisible:
                continue
            if not prim.IsA(UsdGeom.Mesh):
                if prim.IsA(UsdGeom.Gprim):
                    raise ValueError('Convert non-mesh geometry to Mesh before OBJ export: ' + str(prim.GetPath()))
                continue
            mesh = UsdGeom.Mesh(prim)
            points = list(mesh.GetPointsAttr().Get() or [])
            counts = list(mesh.GetFaceVertexCountsAttr().Get() or [])
            indices = list(mesh.GetFaceVertexIndicesAttr().Get() or [])
            if (any(count < 3 for count in counts) or sum(counts) != len(indices)
                    or any(index < 0 or index >= len(points) for index in indices)):
                raise ValueError('Invalid USD mesh topology: ' + str(prim.GetPath()))
            if not counts:
                continue
            matrix = cache.GetLocalToWorldTransform(prim)
            transformed = [matrix.Transform(Gf.Vec3d(point)) for point in points]
            st = UsdGeom.PrimvarsAPI(mesh).GetPrimvar('st')
            uv = list(st.ComputeFlattened() or []) if st else []
            normals = list(mesh.GetNormalsAttr().Get() or [])
            if normals and abs(matrix.GetDeterminant()) < 1e-15:
                raise ValueError('Cannot transform normals through a singular matrix')
            normal_matrix = matrix.GetInverse().GetTranspose() if normals else None
            reverse = ((mesh.GetOrientationAttr().Get() == UsdGeom.Tokens.leftHanded)
                       != (matrix.GetDeterminant() < 0))
            holes = set(mesh.GetHoleIndicesAttr().Get() or [])
            stream.write('o ' + str(prim.GetPath()).strip('/').replace('/', '_') + '\n')
            for point in transformed:
                stream.write('v ' + _numbers(point) + '\n')
            cursor = 0
            for face_index, count in enumerate(counts):
                face = []
                for corner_index in range(cursor, cursor + count):
                    vertex = indices[corner_index]
                    texture_index = normal_index = None
                    if face_index not in holes:
                        if uv:
                            value = _channel(uv, st.GetInterpolation(), face_index, vertex, corner_index)
                            if len(value) < 2:
                                raise ValueError('OBJ UVs need two components')
                            stream.write('vt ' + _numbers(value[:2]) + '\n')
                            uv_offset += 1
                            texture_index = uv_offset
                        if normals:
                            value = _channel(normals, mesh.GetNormalsInterpolation(), face_index, vertex, corner_index)
                            normal = normal_matrix.TransformDir(Gf.Vec3d(value))
                            if normal.GetLength() == 0:
                                raise ValueError('Cannot export a zero-length normal')
                            normal.Normalize()
                            stream.write('vn ' + _numbers(normal) + '\n')
                            normal_offset += 1
                            normal_index = normal_offset
                    face.append((vertex, texture_index, normal_index))
                cursor += count
                if face_index in holes:
                    continue
                if reverse:
                    face.reverse()
                faces = triangulate(face, transformed) if options.get('triangulate', False) else [face]
                for polygon in faces:
                    corners = []
                    for vertex, texture_index, normal_index in polygon:
                        value = str(vertex_offset + vertex + 1)
                        if texture_index is not None or normal_index is not None:
                            value += '/' + (str(texture_index) if texture_index is not None else '')
                            if normal_index is not None:
                                value += '/' + str(normal_index)
                        corners.append(value)
                    stream.write('f ' + ' '.join(corners) + '\n')
            vertex_offset += len(points)
            mesh_count += 1
    if not mesh_count:
        raise ValueError('No visible polygon meshes to export')
