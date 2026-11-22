# Stageviz architecture

Stageviz is a Qt desktop application built around an OpenUSD stage. USD owns
composition, schemas, transforms and scene evaluation. Stageviz supplies editing
commands, application state, widgets and viewport presentation. The current
integration targets **OpenUSD 25.11**. The viewport is designed and primarily
optimized for **Hydra Storm**, USD's rasterizing render delegate. Some of the ID
passes use Storm-specific classes and shaders; using Hgi does not make those
passes interchangeable with arbitrary render delegates.

## Contents

- [Where to start](#where-to-start)
- [Widgets and application state](#widgets-and-application-state)
- [How an edit reaches the screen](#how-an-edit-reaches-the-screen)
- [The viewport and renderer](#the-viewport-and-renderer)
  - [Warming without changing the picture](#warming-without-changing-the-picture)
  - [Time and caches](#time-and-caches)
- [Selection from UI to pixels](#selection-from-ui-to-pixels)
  - [Click and sweep selection](#click-and-sweep-selection)
  - [Mapping paths and nested instance indices](#mapping-paths-and-nested-instance-indices)
  - [Drawing the selection](#drawing-the-selection)
- [Surface shading and viewport effects](#surface-shading-and-viewport-effects)
- [Materials, MaterialX and OpenPBR](#materials-materialx-and-openpbr)
  - [Material editing and library discovery](#material-editing-and-library-discovery)
  - [Material previews](#material-previews)
- [Performance choices and their boundaries](#performance-choices-and-their-boundaries)
- [Using USD without duplicating it](#using-usd-without-duplicating-it)
- [Keeping changes manageable](#keeping-changes-manageable)

## Where to start

| Area | Responsibility | Main entry points |
| --- | --- | --- |
| Application | Startup, main window, menus and docks | `sources/app/application.cpp`, `viewer.cpp` |
| Session | Active stage, selection, layer state, notifications and saving | `sources/core/session.cpp`, `viewcontext.cpp` |
| Editing | Undoable operations and their asynchronous completion | `sources/commands/`, `sources/core/commandstack.cpp` |
| USD helpers | Shared path, bounds, payload and authoring operations | `sources/usd/` |
| Viewport | Qt interaction, camera, selection, lights and transform tools | `sources/rendering/viewportwidget.cpp`, `viewporthost.h` |
| Rendering | Hydra engines, scene indices, selection mapping and effects | `renderengine.cpp`, `renderselection.cpp`, `rendertask.cpp` |
| Inspectors | Outliner, properties, layers and material tools | `sources/outliner/`, `properties/`, `layers/`, `materials/` |
| Automation | Embedded Python bindings and external MCP access | `sources/python/`, `sources/mcp/` |

Paths in the rendering row are relative to `sources/rendering/`.

## Widgets and application state

The main `Viewer` assembles the docked tools and connects their actions to the
current `Session`. A widget presents or edits that state; it should not maintain
an independent copy of the document or selection.

| Component | What it owns or presents | How it stays connected |
| --- | --- | --- |
| `Viewer` | Main-window actions, docks and framing commands | Coordinates Session and the active render view |
| `RenderView` | Viewport, timeline and transform controls | Forwards document/view updates into `ViewportWidget` |
| `ViewportWidget` | OpenGL widget, input routing and viewport components | Implements `ViewportHost` for selection, lights and transforms |
| `ViewContext` | Access to the stage, locks, selection and view state for a tool/view | Lets components use their supplied context rather than assume a global viewport |
| `SelectionList` | Ordered selected USD paths and the lead selection | Shared source of truth for Outliner, properties and viewport |
| `ViewState` / `ViewCamera` | Display preferences and camera state | Supply rendering, navigation and framing settings |
| `Outliner` / `IndexTree` | USD hierarchy, expansion and selection presentation | Apply Session notices and reflect shared selection |
| `PropertyTree` | Prim/property inspection and component editors | Rebuild or refresh affected content; commit edits through commands |
| `LayerDialog` / `LayerTree` | Layer hierarchy, state and edit-target actions | Observe Session and use layer commands |
| `MaterialDialog` | Material browser, graph, property tree and swatches | Coordinates material commands and shared preview rendering |

`ViewportHost` keeps interaction tools separate from the Qt widget's event
handlers. `ViewportSelection` owns picking and sweep acceleration;
`ViewportTransform` owns drag state and prepared transforms; `ViewportLights`
handles light-guide interaction. `ViewportAuxiliary` supplies editor geometry.
These components cooperate through the host instead of each building a renderer.

The Outliner uses pointer-backed virtual selection in `IndexTree`, with sets for
selected items and highlighted ancestors. Painting can test membership without
asking Qt to build a large native selection range for every update. USD paths
remain the document identity; item pointers are only a UI acceleration. This is
why automation must read `Session` selection rather than rely on
`QTreeWidgetItem::isSelected()`.

Widget layout and tooltips belong in Designer `.ui` files. The shared `UI` service
supplies icons and colors. Substantial widgets use a small public interface and
private `Data` state; the Layer Editor is the reference pattern.

Source entry points: [Viewer](../sources/app/viewer.cpp),
[RenderView](../sources/rendering/renderview.cpp),
[ViewportHost](../sources/rendering/viewporthost.h),
[IndexTree](../sources/outliner/indextree.cpp),
[PropertyTree](../sources/properties/propertytree.cpp) and
[MaterialDialog](../sources/materials/materialdialog.cpp).

## How an edit reaches the screen

```text
Widget or Python command
    -> CommandStack -> command worker -> USD edit
    -> USD notices -> Session -> interested widgets and viewport
    -> Hydra synchronization -> rendered frame
```

User edits go through commands so undo restores the original authoring state.
The command stack clears history when it detects a different stage or edit target.
Commands may finish asynchronously: dependent operations wait for Session
completion, rather than assuming dispatch means completion. Workers hold the
stage write lock while editing; scene readers hold the read lock. Qt widgets
remain on the application thread.

Session translates USD notices into `NoticeBatch` updates. Panels derive their
state from the stage and these notifications. Selection is stored centrally as
USD paths; a viewport may project a subset path to its owning geometry for drawing
without changing the actual selection.

Interactive previews are temporary edits. `edit::AttributePreview` captures the
edit target and mapped spec path, then restores that target on cancellation or
before an attribute command is committed. Transform tools have their own prepared
operation state because they also preserve pivots and dependent transforms.

## The viewport and renderer

`RenderView` joins the viewport, timeline and tool controls. `ViewportWidget`
handles Qt events and supplies the small `ViewportHost` interface to selection,
light and transform components. `RenderEngine` also serves offscreen rendering
and material previews, keeping the rendering implementation shared.

A frame follows this order:

1. Choose the document stage, including its isolated population if needed.
2. Configure camera, viewport, lighting and presentation settings.
3. Call USD's `PrepareBatch` to synchronize pending scene-index updates.
4. Refresh affected materials, resolve selection and assemble task parameters.
5. Warm missing document resources, then submit the visible document pass.
6. Collect requested captures and finish the document Hgi frame.
7. Prepare and render auxiliary geometry, then the gizmo overlay.

The custom render task runs after scene rendering and before presentation. It
owns the shared object-ID pass, selection outlines, ambient occlusion and display
color transform. The grid and gizmo are separate passes so they do not contribute
to document AO. The gizmo clears depth while retaining the rendered color.

### Warming without changing the picture

Display-mode preparation, selection-resource preparation and gizmo preparation
serve different resources. Their state stays with the engine or task that owns
those resources. `warmDocument` and `warmGizmo` group the viewport-level policy;
`ImagingGLEngine::warmDisplayModes` handles alternate representations; the render
task prepares selection resources in Hydra's prepare/execute lifecycle.

Hidden warm-up submissions do not present their images. The visible submission
restores the requested settings. Cache validity checks avoid repeating work on
ordinary frames. Full and isolated document engines share an Hgi to reuse Storm
resources; auxiliary and gizmo engines keep their own presentation state.

### Time and caches

Rendering, sweep bounds, visibility, framing and displayed gizmo transforms use
the viewport time. Bounds caches receive an explicit `UsdTimeCode`; the sweep
cache also records its evaluation time. Default-time USD helpers remain available
for authoring commands and existing Session queries. Displaying a frame does not
change the application's default-value authoring policy. Routine scene-range
updates reuse the bounds supplied by Session; they do not recompute the whole
stage in the viewport's notification handler. Frame-all and frame-selected
requests evaluate bounds at the viewport time on demand.

Caches are derived data. Their owner must define what invalidates them: USD
notices for scene changes, scene-index notices for Hydra topology, or explicit
viewport changes such as time and camera movement. USD's mutable bounds caches
are synchronized or private to each worker.

## Selection from UI to pixels

Selection has three representations, each with a different job:

```text
Ordered USD paths in SelectionList
    -> viewport paths / instance-proxy targets
    -> Hydra drawable IDs and flattened instance IDs
    -> GPU membership data and the outline image
```

Keeping these separate is important. A selected instance proxy must retain its
identity in the UI even though Storm draws it through a shared prototype. A
geometry-subset path can remain selected in the document while its owning
geometry is highlighted in the viewport.

### Click and sweep selection

Click selection uses the viewport's picking path, including GPU ID picking for
cycling through overlapping hits and USD imaging intersection queries where
appropriate. Nearby repeated clicks retain a cycle history; changes to the
scene/time invalidate it. Light guides have their own hit handling because they
are editor representations rather than ordinary document meshes.

Rectangle selection queries a cached bounding-volume hierarchy (BVH). Building
it evaluates candidate geometry, visibility and bounds at the viewport time.
Workers use separate `UsdGeomBBoxCache` instances. Queries prune BVH branches
outside the selection frustum and combine worker results deterministically.
Scene changes or a different evaluation time invalidate the index; repeated
sweeps reuse it. A bounds/frustum sweep is not the same operation as selecting
only pixels visible through the depth buffer.

Modifiers are applied when publishing the resulting USD paths through the
selection command. The viewport, Outliner and Property Editor then observe the
same selection. Visible-object capture is a separate ID-buffer workflow; callers
should reuse `ViewportCapture` and the existing render capture API.

### Mapping paths and nested instance indices

[RenderSelection](../sources/rendering/renderselection.cpp) owns the optimized
USD-to-Hydra mapping and its scene-index observer. Ordinary leaf geometry can use
direct paths. Hierarchies, native instances and instance proxies need mapping
through the imaging scene index.

The mapper reduces overlapping roots, caches path classification and mapped
seeds, and groups selections that share a prototype **before** expanding its
descendants. It preserves outer instance context when grouping, avoiding false
combinations between nested branches. Node classification can run in parallel.

Nested instance indices are flattened in the same outer-to-inner convention
used by USD's selection observer: each level combines the accumulated prefix
with the level's instance count and selected index. The effective calculation
is `prefix * count + index`. Instance-count queries are cached during mapping.
This is how selecting one copy avoids selecting every copy of its prototype.

The most recent resolved root set and result are cached. Scene-index topology
notifications advance the mapping generation and invalidate dependent results.
`RenderEngine` also caches the resolved selection and lead-selection inputs.
Mapping runs after `PrepareBatch` so it sees pending imaging changes.

The native USD selection implementation is a regression reference. Stageviz
retains its grouped traversal and caches for large selections; the architecture
audit moved that implementation into its own owner rather than replacing it.

### Drawing the selection

[RenderTask](../sources/rendering/rendertask.cpp) uses a shared, single-sample
scene-ID/depth pass and a selected-geometry ID pass. Selected geometry reads the
scene depth without clearing or writing it. Equal-depth selected fragments can
therefore remain highlighted even when a coincident unselected object won the
scene-ID pass; occluded geometry still fails the depth test.

Membership is represented by whole-drawable IDs or sparse `(primId, instanceId)`
pairs. The selection pass restricts its roots to the relevant drawables and uses
a dedicated Storm shader-storage buffer for membership. That buffer is allocated
separately so its indexing does not depend on offsets in Storm's aggregated
buffers. Changing selection updates data instead of creating a shader variant.
Sorted membership keys are searched in the shader.

The outline composite uses a direct ID mask for whole primitives and a sorted
sparse instance table with binary search. Role values distinguish the lead
selection from the remaining selection. Mask contents, capacities and GPU
resources are reused when valid. The shader examines neighboring selected pixels
to produce the outline; it does not traverse a CPU selection list per pixel.

This path deliberately uses Storm's render-pass, resource-registry and render-buffer
APIs. Its shaders import USD's `hdSt`/`hdx` render-pass pieces. The custom ID pass
checks for an `HdStRenderDelegate`; another delegate is not an equivalent tested
backend for these effects.

## Surface shading and viewport effects

A material's **surface shader** describes the appearance of scene geometry. USD
materials connect surface outputs to shader networks; Storm consumes the composed
network, and its MaterialX path uses USD's MaterialX filtering/generation support.
Stageviz authors and inspects these networks, rather than implementing its own
OpenPBR or Standard Surface lighting model.

`RenderSceneIndex` applies viewport presentation overrides such as scene-material,
clay/custom-material and double-sided settings. Those overrides affect the
rendering view of the scene, not the authored material bindings in the document.
Its indexed geometry sets allow targeted dirty notices instead of a complete
scene traversal every time a display override changes. Material context changes
can require resynchronizing the owning material so Hydra resolves the newly
composed network.

The custom viewport shaders have a different responsibility:

| Resource | Purpose |
| --- | --- |
| [SceneId.glslfx](../resources/SceneId.glslfx) | Emit primitive, instance and element IDs using USD's drawing coordinates for picking/capture and selection support |
| [SelectionId.glslfx](../resources/SelectionId.glslfx) | Filter selected primitive/instance membership while reusing scene depth |
| [Render.glslfx](../resources/Render.glslfx) | Screen-space AO evaluation/blur/composite and selection-outline composition |
| [ColorTransform](../sources/rendering/colormanagement.cpp) | Use OCIO's generated GPU processor with cached Hgi shaders and LUT resources |

AO operates on the document depth/color. Display color conversion follows scene
AO, and selection outlines are composited afterward so UI highlight colors are
not transformed as scene lighting. Depth and integer ID outputs stay raw. Grid
and gizmo rendering use separate passes; they do not become part of document AO.
These effects complement Storm surface shading rather than replace it.

## Materials, MaterialX and OpenPBR

`UsdShadeMaterial` and `UsdShadeShader` are the authored USD containers and
connections. MaterialX supplies node definitions, types, metadata and shader
implementations; OpenPBR and Standard Surface are surface models available through
that library. They are not separate render delegates.

| Material family | Authored shader ID | Surface connection / policy |
| --- | --- | --- |
| USD Preview Surface | `UsdPreviewSurface` | Universal USD surface output and the USD Preview helper-node family |
| MaterialX Standard Surface | `ND_standard_surface_surfaceshader` | `outputs:mtlx:surface`; creation authors the supported canonical material parameters |
| MaterialX OpenPBR Surface | `ND_open_pbr_surface_surfaceshader` | `outputs:mtlx:surface`; creation keeps the shader minimal and uses installed NodeDef defaults, authoring inputs as edited |

Availability and appearance depend on the installed USD/MaterialX libraries and
Storm's implementation. Exposing a NodeDef's complete interface in an editor is
not a claim that every feature has identical support in every renderer. Stageviz
primarily validates its interactive material behavior against Storm.

### Material editing and library discovery

[MaterialUtils](../sources/materials/materialutils.cpp) centralizes surface
resolution, connected input values, node definitions, type conversion and
import/export. Surface inspection follows connected material outputs first
(universal, then `mtlx`, then other surface outputs), with a compatibility fallback
for incomplete older files. It does not simply choose the first descendant shader.
That inspection preference should not be confused with a render delegate's own
render-context selection.

Library discovery starts with `UsdMtlxSearchPaths()`, then considers configured
environment and application locations. MaterialX XML supplies definitions and UI
metadata; inherited NodeDef interfaces are resolved and supplemented from USD's
Sdr registry. The resulting definitions are cached. This allows property editors
to expose defaults, groups and inputs that are not yet authored in USD.

`MaterialGraph` displays connections, `MaterialTree` presents editable parameters,
and `MaterialMenu` offers compatible nodes. Their user-facing edits go through
material/property commands. Connection checks preserve the distinction between
USD Preview and MaterialX node families and reject unsupported cross-family links.
The creation menu excludes MaterialX `ND_UsdPrimvarReader_*` compatibility nodes
because this Storm integration cannot generate them reliably. Their definitions
remain discoverable for inspecting imported networks; native MaterialX geometric
nodes and USD Preview primvar readers remain available in their respective menus.

MaterialX import uses `UsdMtlxRead`, avoids naming collisions, and anchors imported
asset paths to the source document directory. Export walks supported MaterialX
connections and emits a MaterialX document, rejecting unsupported non-MaterialX
nodes rather than silently treating an arbitrary USD network as exportable.

### Material previews

[MaterialRenderer](../sources/materials/materialrenderer.cpp) reuses offscreen
`RenderEngine` instances. It keeps a lightweight parameter-preview context and a
persistent authored-network context warm. Interactive network edits reuse that
network context, avoiding another Storm initialization for each scrub.

Requests are queued/coalesced by material path, with priority for interactive
work. Rendering stays on the GUI thread for graphics-context ownership. The image
cache is bounded (128 MiB accounting for linear and display images); it does not
grow without limit as more materials are visited. Compact network snapshots and
targeted invalidation let unrelated swatches stay cached. Linear render data and
display conversion are kept distinct so previews follow the selected color view.

## Performance choices and their boundaries

The goal is a responsive Storm viewport on large stages. Most optimizations avoid
repeating composition queries, tree work, resource creation or shader preparation;
they do not replace USD's scene evaluation.

| Area | What is reused or reduced | What must invalidate it |
| --- | --- | --- |
| Outliner | Pointer/set selection, ancestor membership and incremental notice handling | Row lifetime, hierarchy changes and selection changes |
| Sweep | BVH, parallel bounds construction and pruned spatial queries | Scene/bounds/visibility changes and evaluation time |
| USD-to-Hydra selection | Direct-path fast path, grouped prototypes, mapped seeds and resolved roots | Relevant scene-index changes and selected paths |
| GPU selection | ID-to-path lookup, selected roots, membership buffers and role masks | Rprim-index version, membership and resource size/settings |
| Display modes/gizmos | Hidden preparation and retained Storm resources | Geometry/dependency changes and preparation settings |
| Isolation | A masked USD stage with required material/light dependencies; cached engines and shared document Hgi | Population/composition dependencies, source changes and mask |
| Display overrides | Indexed geometry sets and targeted dirtying | Added/removed scene-index prims and override state |
| Transforms | Prepared operation state, normalized authoring and cached drag-start transforms | New gesture, target selection or relevant stage changes |
| Material previews | Persistent contexts, compact networks, queued work and bounded images | Affected material/network changes and display settings |
| Color management | Generated shader, LUT textures and samplers | OCIO settings, input/output requirements and Hgi lifetime |
| Bounds | Shared synchronized USD cache; worker-local caches where parallel | USD changes and requested evaluation time |

Isolation uses USD population and includes required dependencies; it is not merely
a beauty-pass visibility filter. The full and isolated document engines share an
Hgi to reuse Storm resources, and returning to a previously prepared view should
not repeat its preparation unless the source/settings changed.

Do not add whole-stage work to frequent notification handlers. The audit's extra
viewport bounds query on every Session bounds update caused a reproducible
20–25% parent-edit slowdown in the focused synthetic Debug benchmark and was
reverted. The viewport again consumes Session's computed range; explicit framing
still evaluates the requested time. This is a concrete example of why a seemingly
small correctness/organization change needs performance validation.

Use [the focused selection comparison](../CTest/benchmark/compare_selection_pipeline.py)
and [benchmark guide](../CTest/benchmark/README.md) for before/after checks. Keep
SDK, build configuration, scene, viewport, settings and selection seed equal;
separate first-use work, repeated mapping and cached redraws. Include populated
Outliner branches, instances/proxies and parent edits with undo. Native
[phase logging](performance-logging.md) helps locate costs but adds instrumentation
overhead. Readback-inclusive response time is not a GPU-only measurement, and
small Debug differences should not be presented as a universal speedup.

## Using USD without duplicating it

Use schema APIs, `UsdGeomBBoxCache`, `UsdGeomXformCache`, `UsdNamespaceEditor`,
scene-index schemas and Hgi where they match the operation. Put shared application
policy in the existing USD/material helpers, rather than copying it into widgets.

Some differences are intentional:

- `RenderSelection` retains grouped instance traversal and cached path mapping.
  USD 25.11's selection scene index expands descendants per selected path; its
  observer initially traverses the input scene. Native selection is used as a
  regression reference, not assumed to have the same large-selection cost.
- Namespace moves normally use `UsdNamespaceEditor`. A guarded Sdf batch path
  serves eligible large edits without composition dependencies.
- Stageviz's geometry classification includes analytic primitives. USD 25.11's
  `HdPrimTypeIsGprim` has a different type set, so it is not a direct replacement.
- Sorted root reduction delegates to `SdfPath::RemoveDescendentPaths`. The
  depth-ordered helper reuses that reduction and then restores its documented
  ordering; stable selection-order helpers keep their separate contracts.

The reference implementations are USD 25.11's
[selection scene index](https://github.com/PixarAnimationStudios/OpenUSD/blob/v25.11/pxr/usdImaging/usdImaging/selectionSceneIndex.cpp),
[selection observer](https://github.com/PixarAnimationStudios/OpenUSD/blob/v25.11/pxr/imaging/hdx/selectionSceneIndexObserver.cpp)
and [usdview viewport](https://github.com/PixarAnimationStudios/OpenUSD/blob/v25.11/pxr/usdImaging/usdviewq/stageView.py).

The `ImagingGLEngine` adapter is specific to USD 25.11. Future SDK upgrades need
an explicit compatibility review; the rest of the application should not acquire
additional dependencies on its protected internals.

## Keeping changes manageable

Substantial Qt components use a small public class and a private implementation.
Keep private declarations in lifecycle/init, utilities, actions/slots and accessors
order, followed by a `Data` struct. Keep related definitions together. Small value
objects and stateless helpers do not need an extra implementation layer.

Designer-owned controls and tooltips belong in `.ui` files. Icons come from the
shared `UI` service. Add behavior to the owning feature and extract a common helper
when multiple callers need the same semantics.

For verification, use native command/USD tests, GUI tests for widget behavior,
and embedded Python suites for exposed workflows. The external MCP controller
can independently inspect the live stage; embedded tests must not synchronously
call back into the same application-thread MCP server.

See [test coverage](../CTest/README.md), [Python API](../skills/references/python-api.md),
[MCP](MCP.md), [path collections](path-collections.md) and
[performance logging](performance-logging.md) for details.
