# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 - present Mikael Sundell

"""Checkbox read/paint and branch-edit benchmark for a disposable MCP process.

The external controller owns process lifetime and independent MCP oracles.
Use identical fixture/output directories and settings for before/after runs.
"""
import json
from pathlib import Path
import statistics
import sys
import time

import stageviz
from pxr import Usd
from PySide6 import QtCore, QtGui, QtWidgets

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'python'))
import testbench as tb


def make_fixture(directory, groups=500, leaves=400):
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    (directory / 'asset.usda').write_text('#usda 1.0\ndef Xform "Asset" {\ndef Cube "Cube" {}\n}\n')
    (directory / 'black.usda').write_text('''#usda 1.0
    over "World" {
        over "Black_off" {
            def Xform "Gate" (prepend payload = @asset.usda@</Asset>) {}
        }
    }
    ''')
    lines = ['#usda 1.0', '(', 'defaultPrim = "World"', 'subLayers = [@black.usda@]', ')', 'def Xform "World" {']
    for group in range(groups):
        lines.append('def Xform "Group%04d" {' % group)
        lines.append('def Xform "Gate" (prepend payload = @asset.usda@</Asset>) {}')
        lines.extend('def Xform "Item%04d" {}' % item for item in range(leaves))
        lines.append('}')
    lines.extend(['def Xform "Black_off" {}', 'def Xform "Destination" {}', '}'])
    file = directory / 'heavy.usda'
    file.write_text('\n'.join(lines) + '\n')
    return str(file)


class PayloadIndicatorBenchmark:
    def __init__(self, fixture, output, groups=500):
        self.fixture = str(fixture)
        self.output = Path(output)
        self.output.mkdir(parents=True, exist_ok=True)
        self.groups = groups
        self.report = {'phases': []}
        self.gates = ['/World/Group%04d/Gate' % i for i in range(groups)] + ['/World/Black_off/Gate']

    def tree(self):
        for widget in QtWidgets.QApplication.allWidgets():
            try:
                if widget.objectName() == 'outlinerTree' and widget.isVisible():
                    widget.model()
                    return widget
            except RuntimeError:
                continue
        raise AssertionError('visible Outliner')

    def index(self, path):
        tree = self.tree()
        model = tree.model()
        parent = QtCore.QModelIndex()
        wanted = ['/'] + ['/' + '/'.join(path.strip('/').split('/')[:i + 1])
                           for i in range(len(path.strip('/').split('/')))] if path != '/' else ['/']
        for part in wanted:
            found = None
            for row in range(model.rowCount(parent)):
                index = model.index(row, 0, parent)
                if index.data(int(QtCore.Qt.ItemDataRole.UserRole) + 1) == part:
                    found = index
                    break
            assert found is not None, part
            parent = found
        return parent

    def state(self, path):
        state = self.index(path).data(QtCore.Qt.ItemDataRole.CheckStateRole)
        return state.value if hasattr(state, 'value') else state

    def checkpoint(self, label):
        assert tb.wait_for_commands(timeout=120)
        tb.process_events()
        tree = self.tree()
        root = self.index('/')
        # Fully populate one branch for the repaint measurements.
        tree.setExpanded(root, True)
        tree.setExpanded(self.index('/World'), True)
        tree.setExpanded(self.index('/World/Group0000'), True)
        tree.scrollTo(root)
        tb.process_events()
        model = tree.model()
        cold = []
        for _ in range(7):
            model.dataChanged.emit(root, root, [int(QtCore.Qt.ItemDataRole.CheckStateRole)])
            start = time.perf_counter()
            root.data(QtCore.Qt.ItemDataRole.CheckStateRole)
            cold.append((time.perf_counter() - start) * 1000)
        warm = []
        for _ in range(7):
            start = time.perf_counter()
            for _ in range(25):
                root.data(QtCore.Qt.ItemDataRole.CheckStateRole)
            warm.append((time.perf_counter() - start) * 1000 / 25)
        paint = []
        pixmap = QtGui.QPixmap(tree.viewport().size())
        for _ in range(7):
            start = time.perf_counter()
            tree.viewport().render(pixmap)
            paint.append((time.perf_counter() - start) * 1000)
        stage = stageviz.session().stage()
        composed = sum(1 for _ in stage.TraverseAll())
        # This fixture has three authored levels beneath the pseudo-root. Sum
        # populated row counts without wrapping every leaf as a Python object.
        def count(index, depth):
            n = model.rowCount(index)
            return n if depth == 3 else n + sum(count(model.index(i, 0, index), depth + 1) for i in range(n))
        rows = count(QtCore.QModelIndex(), 0)
        result = dict(label=label, composed_prims=composed, outliner_rows=rows,
                      states={p: self.state(p) for p in ('/', '/World', '/World/Black_off', '/World/Group0000')},
                      cold_read_ms=statistics.median(cold), warm_read_ms=statistics.median(warm),
                      forced_paint_ms=statistics.median(paint), samples=dict(cold=cold, warm=warm, paint=paint),
                      muted=stage.GetMutedLayers())
        self.report['phases'].append(result)
        (self.output / 'report.json').write_text(json.dumps(self.report, indent=2))
        assert tb.viewer().grab().save(str(self.output / (label + '.png')))
        print('PAYLOAD_INDICATOR_BENCHMARK', json.dumps(result), flush=True)

    def setup(self):
        start = time.perf_counter()
        assert stageviz.session().loadFromFile(self.fixture, policy=stageviz.LoadNone)
        assert tb.wait_for_commands(timeout=120)
        tb.process_events()
        self.report['load_ms'] = (time.perf_counter() - start) * 1000
        assert sum(1 for _ in stageviz.session().stage().TraverseAll()) >= 200000
        self.checkpoint('unloaded')

    def load(self):
        start = time.perf_counter()
        stageviz.command.load_payloads(self.gates)
        assert tb.wait_for_commands(timeout=120)
        self.report['load_payloads_ms'] = (time.perf_counter() - start) * 1000
        assert all(stageviz.session().stage().GetPrimAtPath(p).IsLoaded() for p in self.gates)
        self.checkpoint('loaded')

    def mute(self):
        start = time.perf_counter()
        stageviz.command.set_layer_muted(str(Path(self.fixture).with_name('black.usda')), True)
        assert tb.wait_for_commands(timeout=120)
        self.report['mute_ms'] = (time.perf_counter() - start) * 1000
        assert not stageviz.session().stage().GetPrimAtPath('/World/Black_off/Gate')
        self.checkpoint('muted')

    def parent_move(self):
        moved = '/World/Group0250'
        stageviz.command.select_paths([moved + '/Item0001', moved])
        assert tb.wait_for_commands()
        start = time.perf_counter()
        stageviz.command.move_path([moved], '/World/Destination')
        assert tb.wait_for_commands(timeout=120)
        self.report['parent_move_ms'] = (time.perf_counter() - start) * 1000
        assert stageviz.session().stage().GetPrimAtPath('/World/Destination/Group0250/Item0001')
        assert not stageviz.session().stage().GetPrimAtPath(moved)
        stageviz.command.undo()
        assert tb.wait_for_commands(timeout=120)
        assert stageviz.session().stage().GetPrimAtPath(moved + '/Item0001')
        self.checkpoint('move_undone')

    def unload(self):
        stageviz.command.unload_payloads(self.gates[:-1])
        assert tb.wait_for_commands(timeout=120)
        assert all(not stageviz.session().stage().GetPrimAtPath(p).IsLoaded() for p in self.gates[:-1])
        self.checkpoint('unloaded_again')
