# Interaction utility audit

The sixth iteration starts at baseline commit `f991278`. The pre-change source
and patch are also in the OpenUSD `stageviz-investigation/test6-interaction-utilities/before`
directory. No OpenUSD SDK source change is required for this iteration.

## Shared implementations

Use `stageviz::path` in `sources/usd/usdutils.h`:

* `PathIndex`: exact membership and nearest/outermost ancestor coverage, built
  once per operation. Coverage is proportional to path depth, not root count.
* `PathRelations`: ancestor/descendant intersection for repeated notice or mask
  queries. It indexes distinct path prefixes once.
* `PathCollector`: append-only stable duplicate suppression. The output list
  must not be changed independently while its collector exists.
* `stableRootPaths`: unique minimal roots in surviving input order.
* `sortedRootPaths`: sort once using USD path order, then discard adjacent
  descendants. Used for scene counts and removal traversal where input/depth
  order is unnecessary. This preserves the efficient original statistics
  algorithm; the initial general-reducer migration added about 0.26 seconds to
  warm-sweep statistics and was corrected after the live timing review.
* Existing `topLevelPaths`, `uniquePaths`, and `minimalRootPaths` delegate to the
  same primitives, retaining their different ordering/empty/duplicate contracts.
* `removeTokens`: one removal-membership index and one pass through the order.
* `layer::ensureAttributeSpec`: typed Sdf-only spec creation for preflighted
  bulk authoring. It accepts mapped layer paths and never queries a USD stage.

Path indexes preserve exact `SdfPath::HasPrefix` semantics. They deliberately
do not turn property paths into prim paths. Callers normalize properties where
their previous behavior did so. Empty selection covers nothing; empty isolation
masks still cover everything at the caller. Relative parent paths use USD's
finite prefix enumeration rather than repeatedly asking for the logical parent.

## Call-site review

| Area | Finding and action |
| --- | --- |
| Selection commands | All three inversion variants now build one coverage index outside the domain loop. |
| Transform resolution | Indexed source-root mapping, stable deduplication/root reduction, preservation membership, input-to-target mapping, and capture validation replace nested selection scans. A single USD transform cache serves operation capture and preparation. |
| Viewport transform | Visible selection uses a collector and one prepared mask index; pivot setup does not gather the same visible selection twice. |
| Property, visibility, copy/instance/group commands | Append-only path notification outputs use collectors rather than repeated list membership. |
| Payload utilities | Bulk payload outputs and resolution use the shared collector. Existing set-based traversals remain. |
| Outliner | Override-cache namespace filtering and path remapping use prepared coverage indexes; context-menu mask membership and variant target union are indexed. |
| Property editor | Selection/notice overlap uses one relation index; context-menu prim collection uses a collector. |
| Viewport isolation | Bulk selection visibility uses a prepared relation index. |
| Scene statistics | Transform-only property notices retain total and selected topology counts. Other/unknown notices remain conservative. Selection, stage replacement, and close invalidate the relevant cached counts. Root filtering uses the shared utility. |
| SelectionList | Already maintains hash membership and ordering, including empty-path semantics; retained rather than replacing it with a collector that intentionally rejects empties. |
| Render scene index / BVH | Existing sorted range removal, hash lookups, BVH query/build loops, and proxy-hit ancestor classification retained. |
| Layer tree / session restore | Existing identifier sets, maps, and linear tree traversal retained. JSON/string `contains` is not a linear path-membership search. |
| Material/UI/Python code | Hash maps, string filtering, small schema/enum/action lists, fixed matrix/component loops, and UI construction retained. |
| Namespace load-rule remapping | Ordered first-match mappings retained: choosing the nearest ancestor could change overlapping move semantics. |

The audit searches cover all `sources` C++ headers/implementations, with explicit
review of path loops, list `contains`, `HasPrefix`, `appendUnique`, traversals,
and `SdfChangeBlock` sites. This is a review of loop patterns and invalidation,
not a claim that every remaining loop is a performance bottleneck. Single-item
`appendUnique` and list-based coverage helpers remain available for small calls;
bulk callers should build an index/collector outside their loop.

## Authoring correction

Transform preparation now reads USD state before opening `SdfChangeBlock`,
authors mapped edit-layer specs through Sdf inside the block, and obtains fresh
USD op handles after delivery. Repeated prepared updates likewise batch Sdf
values and keep any generic USD fallback outside the block. Undo removes only
new inert ancestor overs recorded before the edit. Pivot/reset-stack behavior,
instance-root resolution, shared-source preservation, and command undo/redo
remain covered by existing tests plus the new composed/variant regressions.

The follow-up also moves schema and value queries out of property creation and
rename change blocks. Exact property and prim deletions use their Sdf owners,
avoiding a namespace-validation transaction per removed spec; unusual variant
ownership retains the generic namespace fallback. Dependency remapping remains
the caller's responsibility, as it was with the previous deletion helper.
Property batches retain separate scene and mapped spec paths for capture,
authoring, snapshots and undo. One anonymous snapshot layer serves the whole
attribute batch. Root/child variant cases verify that attributes never escape
their variant namespace and that removal/rename restore exact original specs.

The shared bounds cache now receives synchronous invalidation from Session's
USD notice listener, including suppressed/deferred transactions. CacheGuard
clears stale state on its next acquisition. Gizmo bounds can therefore reuse
valid scene bounds without waiting for Qt delivery or carrying a previous
gesture's geometry. Native checks compare with a fresh USD cache immediately
after changes to transforms, inherited transforms, geometry, visibility, and
activation. Session's cached aggregate is marked dirty synchronously as well.

## Boundaries

This change preserves the existing inversion domain (ordinary namespace leaves).
It does not redefine inversion as a visual complement of partial instance-proxy
or geometry-subset selections. World-space bounds still require valid USD
recomputation after edits. These are separate from replacing quadratic searches;
the performance report records their remaining measured costs.
