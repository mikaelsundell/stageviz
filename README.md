stageviz
==================

[![License](https://img.shields.io/badge/license-BSD%203--Clause-blue.svg?style=flat-square)](https://github.com/mikaelsundell/brawtool/blob/master/README.md)

Introduction
------------

This project is an experimental exploration of USD and eventually MaterialX visualization and interaction using C++ and Qt, serving as an alternative implementation of the usdview application. It acts as a testbed for USD functionality, focusing on real-time rendering, Qt-based UI integration, and performance optimizations. Most of its functionality mirrors the Python-based features found in StageView.py and related classes,  in a native C++ implementation for deeper control and efficiency.


Application
------------

todo: Insert application image here with the kitchen scene.

### Functionality

todo: Insert application functionality here


Technical notes
-------------

Coplanar pick limitation

Hydra picking in UsdImagingGLEngine is raster-based and resolve-mode dependent. The pick task renders ID and depth information for a pick frustum and then resolves that buffer into hits, rather than performing an exact analytic ray intersection against geometry. In particular, resolveNearestToCenter uses a pick render plus a customized depth buffer to determine an approximate intersection near the center of the query.

This has important consequences for coplanar or near-coplanar surfaces. When two surfaces lie on the same plane, or are close enough to quantize to the same effective depth in the pick pass, a single-click pick can collapse to a single “winning” surface. In that case only one prim is returned even though multiple surfaces overlap at that location. This behavior is a consequence of the depth-buffer-based picking approach, not an issue with shading normals.

Deep sweeps behave differently. Using resolveDeep, Hydra performs a deep selection over the pick region and can return multiple prims within the frustum, including those obscured by other geometry. This makes sweep-based selection significantly more robust for overlapping or coplanar surfaces.

The same distinction applies to capture workflows:

Single clicks using resolveNearestToCenter can miss coplanar surfaces because the result collapses to one approximate winner near the click center.
Capture using resolveUnique collects a set of unique hits within the frustum and is generally more robust than a single nearest-center query, but it still does not provide the same “see through” behavior as deep selection.
Deep sweeps using resolveDeep are the most reliable method for discovering all overlapping or coplanar surfaces within a screen region.

In practice:

- Use resolveNearestToCenter for fast, conventional click picking.
- Use resolveUnique for visible-set capture when a deduplicated set of hits is desired.
- Use resolveDeep for region-based queries when overlapping, obscured, or coplanar surfaces must be discovered.


Known issues
-------------

### macOS Thread Performance Checker (TPC) warnings in Xcode 16.2+ for debug builds

When running the viewer or plug-in inside Xcode 16.2 or later, you may see warnings such as:

```code
Thread Performance Checker: Thread running at User-interactive quality-of-service class waiting on a lower QoS thread running at Default quality-of-service class.
```

These messages are emitted by macOS’s Thread Performance Checker, not by USD or Hydra.
They occur because Pixar’s USD libraries use Intel TBB worker threads at a lower quality-of-service level than the GUI thread, which causes Xcode to flag a “priority inversion.”
This behavior is normal and does not indicate a bug or performance problem in the application.

To suppress these diagnostics, disable the Thread Performance Checker in your Xcode scheme:

- Open your Xcode project or workspace.
- Choose Product ▸ Scheme ▸ Edit Scheme…
- Select the Run action.
- Open the Diagnostics tab.
- Uncheck Thread Performance Checker.

After turning it off, the messages will no longer appear in the Xcode console.

References
-------------

* Qt6 documentation    
https://doc.qt.io/qt-6

* USD API    
https://openusd.org/release/api/index.html

* USD Tutorials    
https://openusd.org/release/tut_usd_tutorials.html

* USD Cookbook    
https://github.com/ColinKennedy/USD-Cookbook


Project
-------

* GitHub page   
https://github.com/mikaelsundell/stageviz

* Issues   
https://github.com/mikaelsundell/stageviz/issues

## License ##

3rdparty packages and their copyrights:

OpenUSD
Copyright 2022 Pixar

Qt
Copyright (C) 2019 The Qt Company Ltd.