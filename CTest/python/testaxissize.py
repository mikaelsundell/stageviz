# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 - present Mikael Sundell

"""Check corner-axis sizing and immediate repaint in an existing viewer.

Call run() from the embedded interpreter or an external MCP controller. No USD
opinions are changed, and UI size/visibility are restored. MCP callers should
observe stage and selection independently before and after the call.
"""

import contextlib
import io
import json
from pathlib import Path
import sys
import tempfile

import stageviz

if str(Path(__file__).resolve().parent) not in sys.path:
    sys.path.insert(0, str(Path(__file__).resolve().parent))
import testbench as tb


def run():
    # Register the custom viewport's PySide base before QWidget discovery.
    try:
        from PySide6 import QtOpenGLWidgets
    except ImportError:
        from PyQt6 import QtOpenGLWidgets
    _, _, _, widgets = tb.qt()
    root = tb.viewer()
    viewport = tb.find_widget('imagingGLWidget', widgets.QWidget, root)
    api = stageviz.ui()
    medium = api.UIScale.Medium
    state = stageviz.session().viewState()
    old_size = api.axisSize(medium)
    old_visible = state.cameraAxisEnabled()
    stage = stageviz.session().stage()
    before = [layer.ExportToString() for layer in stage.GetLayerStack()]
    selected = list(stageviz.session().paths())
    colors = [tuple(api.color(role))[:3] for role in (api.AxisX, api.AxisY, api.AxisZ)]
    directory = Path(tempfile.mkdtemp(prefix='stageviz-axis-size-'))
    counts = {}
    try:
        state.setCameraAxisEnabled(True)
        for size in (36, 72, 144, 85):
            api.setAxisSize(medium, size)
            assert api.axisSize(medium) == size
            api.setAxisSize(medium, 0)
            api.setAxisSize(medium, -1)
            assert api.axisSize(medium) == size, 'non-positive sizes are ignored'
            tb.process_events(100)
            capture = viewport.grab()
            image = capture.toImage()
            dpr = capture.devicePixelRatio()
            # Count axis-role pixels near the lower-left corner. Grid and scene
            # pixels do not use these saturated UI colors in this fixture.
            extent = min(round((size * 1.3 + 24) * dpr), image.width(), image.height())
            count = 0
            for y in range(image.height() - extent, image.height()):
                for x in range(extent):
                    c = image.pixelColor(x, y)
                    rgb = (c.red(), c.green(), c.blue())
                    if any(all(abs(rgb[i] - color[i]) <= 2 for i in range(3)) for color in colors):
                        count += 1
            assert count > 0, 'visible axis refreshes after changing its UI size'
            counts[size] = count
            assert root.grab().save(str(directory / (str(size) + '.png')))
            with contextlib.redirect_stdout(io.StringIO()):
                snapshot = tb.snapshot_qt_state('axis_size_' + str(size))
            (directory / (str(size) + '-qt.json')).write_text(json.dumps(snapshot, indent=2))
        assert counts[144] > counts[72] > counts[36], ('drawn indicator scales with the UI size', counts)
        assert before == [layer.ExportToString() for layer in stage.GetLayerStack()]
        assert selected == list(stageviz.session().paths())
    finally:
        api.setAxisSize(medium, old_size)
        state.setCameraAxisEnabled(old_visible)
        tb.process_events(100)
    print('PASS axis size API, invalid sizes, immediate repaint, visible scaling, stage preservation and restoration')
    print('Axis pixels:', counts, 'Evidence:', directory)
    return str(directory)
