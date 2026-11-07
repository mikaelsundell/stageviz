# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 - present Mikael Sundell

"""Metadata discovery in Stageviz; run via runpy.run_path.

No scene authoring, delegate initialization, or undo/redo is involved.
"""

import sys
from pathlib import Path
import stageviz

if globals().get("__file__"):
    sys.path.insert(0, str(Path(__file__).resolve().parent))
import testbench as tb


def run():
    session = stageviz.session()
    filename = session.filename()
    paths = list(session.paths())
    stage = session.stage()
    text = stage.GetRootLayer().ExportToString() if stage else None
    tb.snapshot_qt_state("before Hydra metadata discovery")
    plugins = stageviz.HydraRenderer.available_renderers()
    assert isinstance(plugins, list)
    ids = set()
    for plugin in plugins:
        assert set(plugin) == {"id", "display_name", "library_path", "priority", "loaded"}, plugin
        assert isinstance(plugin["id"], str) and plugin["id"] and plugin["id"] not in ids
        assert isinstance(plugin["display_name"], str)
        assert isinstance(plugin["library_path"], str)
        assert type(plugin["priority"]) is int
        assert type(plugin["loaded"]) is bool
        ids.add(plugin["id"])
    assert stageviz.HydraRenderer.available_renderers() == plugins
    try:
        stageviz.HydraRenderer.available_renderers("unexpected argument")
    except TypeError:
        pass
    else:
        raise AssertionError("discovery must reject arguments")
    try:
        stageviz.HydraRenderer()
    except TypeError:
        pass
    else:
        raise AssertionError("phase 1 must not expose a nonfunctional rendering session")
    assert session.filename() == filename and list(session.paths()) == paths
    assert (session.stage().GetRootLayer().ExportToString() if stage else None) == text
    tb.snapshot_qt_state("after Hydra metadata discovery")
    print("PASS: Hydra discovery returns typed metadata and preserves session state")
    return True


_TEST_RESULT = run()
