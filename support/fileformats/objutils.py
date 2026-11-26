# SPDX-License-Identifier: BSD-3-Clause
"""Shared polygon triangulation for the OBJ sample (simple planar polygons)."""


def triangulate(corners, points):
    """Return triangles of corner values, retaining winding and corner attributes.

    Ear clipping handles concave polygons. Project onto the dominant normal plane;
    reject degenerate/non-simple polygons when no valid triangulation is found.
    """
    if len(corners) == 3:
        return [corners]
    vertices = [points[corner[0]] for corner in corners]
    normal = [sum((a[(i + 1) % 3] - b[(i + 1) % 3]) * (a[(i + 2) % 3] + b[(i + 2) % 3])
                  for a, b in zip(vertices, vertices[1:] + vertices[:1])) for i in range(3)]
    axis = max(range(3), key=lambda i: abs(normal[i]))
    axes = [i for i in range(3) if i != axis]
    projected = [(v[axes[0]], v[axes[1]]) for v in vertices]
    # Work near the origin to avoid cancellation for geometry far from world zero.
    origin = projected[0]
    projected = [(x - origin[0], y - origin[1]) for x, y in projected]
    scale = max(max(abs(x), abs(y)) for x, y in projected)
    epsilon = scale * scale * 1e-12

    def cross(a, b, c):
        return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])

    area = sum(a[0] * b[1] - b[0] * a[1] for a, b in zip(projected, projected[1:] + projected[:1]))
    if abs(area) <= epsilon:
        raise ValueError('Cannot triangulate a degenerate polygon')
    sign = 1 if area > 0 else -1
    remaining = list(range(len(corners)))
    result = []
    while len(remaining) > 3:
        for offset, current in enumerate(remaining):
            previous, following = remaining[offset - 1], remaining[(offset + 1) % len(remaining)]
            a, b, c = (projected[i] for i in (previous, current, following))
            if sign * cross(a, b, c) <= epsilon:
                continue
            if any(all(sign * cross(u, v, projected[i]) >= -epsilon for u, v in ((a, b), (b, c), (c, a)))
                   for i in remaining if i not in (previous, current, following)):
                continue
            result.append([corners[i] for i in (previous, current, following)])
            del remaining[offset]
            break
        else:
            raise ValueError('Cannot triangulate polygon: check for duplicate, collinear or intersecting edges')
    result.append([corners[i] for i in remaining])
    return result
