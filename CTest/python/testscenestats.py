# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 - present Mikael Sundell

"""Call run(disposable=True) in a disposable viewer to check close-stage stats."""

from pathlib import Path
import runpy
import stageviz
from pxr import Usd, UsdGeom

_base = runpy.run_path(str(Path(__file__).with_name("testviewport.py")),
                      init_globals={"_VIEWPORT_AUTORUN": False})
tb = _base["tb"]


def run(disposable=False):
    assert disposable, "this check requires a disposable viewer"
    # View changes must not turn this close-stage rendering check into a
    # Save State prompt; testpreservestate covers that workflow separately.
    if tb.action_info("filePreserveState")["checked"]:
        assert tb.trigger_action("filePreserveState")
    assert not tb.action_info("filePreserveState")["checked"]
    helper = _base["ViewportTest"](disposable=True)
    session = stageviz.session()
    if session.isLoaded():
        assert tb.trigger_action("fileClose")
    state = session.viewState()
    state.setGridEnabled(False)
    state.setPerformanceStatsEnabled(False)
    state.setSceneStatsEnabled(True)
    tb.process_events(150)

    def stats_image():
        image = helper.viewport().grab().toImage()
        ratio = image.devicePixelRatio()
        return image.copy(0, 0, round(210 * ratio), round(155 * ratio))

    empty = stats_image()
    path = helper.directory / "scene_stats.usda"
    fixture = Usd.Stage.CreateNew(str(path))
    UsdGeom.Cube.Define(fixture, "/Cube")
    fixture.GetRootLayer().Save()
    assert session.loadFromFile(str(path))
    tb.process_events(150)
    assert stats_image() != empty, "loaded stage displays populated counts"
    helper.snapshot("stats_loaded")
    assert tb.trigger_action("fileClose")
    tb.process_events(150)
    assert not session.isLoaded()
    assert not list(session.paths())
    assert stats_image() == empty, "File > Close restores empty scene statistics"
    helper.snapshot("stats_closed")

    assert session.loadFromFile(str(path))
    tb.process_events(100)
    state.setSceneStatsEnabled(False)
    assert tb.trigger_action("fileClose")
    assert not session.isLoaded()
    state.setSceneStatsEnabled(True)
    tb.process_events(150)
    assert stats_image() == empty, "hidden stats also reset when the stage closes"
    print("PASS close-stage stats, File menu, hidden stats, and Qt image comparison")
    print("Artifacts:", helper.directory)
    return True
