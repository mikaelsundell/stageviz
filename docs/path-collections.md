# Path collections and bulk loops

Use native `SdfPath` values for internal path identity. `QHash<SdfPath, T>` and
`QSet<SdfPath>` require `qtutils.h`, which supplies the shared native hash. Use
`SdfPath::Hash` for standard unordered containers. Convert to text for display,
serialization, generic Qt string roles, or APIs that require text.

Choose the shared utilities in `usdutils.h` by the required behavior:

| Need | Utility | Contract |
| --- | --- | --- |
| Deduplicate an existing list | `path::uniquePaths` | First occurrence wins; ignores empty paths |
| Unique output from a bulk loop | `path::PathCollector` | First occurrence wins; ignores empty paths |
| Repeated exact or ancestor membership | `path::PathIndex` | Build once; exact USD path semantics |
| Overlap in either namespace direction | `path::PathRelations` | Includes ancestors and descendants |
| Selected roots in input order | `path::stableRootPaths` | Removes duplicates and covered children |
| Selected roots in USD namespace order | `path::sortedRootPaths` | Uses native USD namespace ordering |
| Selected roots in depth/lexical order | `path::minimalRootPaths` | Retains the existing depth/text contract |
| One or two unique additions | `path::appendUnique` | Avoids allocating an index for a tiny list |

Keep a collector alive across the whole bulk loop. Its output list must not be
modified independently between appends. After the final append, the output can be
consumed or replaced, but that collector must not be used again. Existing output
is indexed as-is; it does not remove earlier duplicates or empty values. Avoid
constructing a new
collector for every appended path; that would rebuild the index each time.

Do not use these helpers to silently normalize property paths, resolve instance
proxies, or turn an empty mask into a match-all query. Those policies belong at
the appropriate selection/command boundary. SelectionList also retains empty
paths, so it cannot use PathCollector's nonempty-output contract.

Preserve observable ordering. Native USD path order and lexical text order are
different contracts. `SdfPath::GetString()` returns a cached string reference;
a lexical comparator is not itself a repeated QString allocation. UI ordering,
first matching payload, notification barriers, and undo state must survive a
performance cleanup. The legacy `path::topLevelPaths` retains duplicate and empty
inputs, so it is not interchangeable with `stableRootPaths`.

Keep true string identifiers as strings: layer identifiers, MaterialX node IDs,
port names, composite renderer cache keys, search labels, and generic Qt roles.
A path embedded in a composite key is not equivalent to that key's identity.

Before changing a traversal, distinguish repeated output deduplication from
repeated traversal. Removing overlapping roots or pruning visited descendants
can change first-visit order and eligibility behavior. Measure those changes
separately and verify the same ordered results, USD state, and undo/redo.

Use authoritative USD summaries to rule out empty domains before a full walk.
For example, an empty `UsdStage::GetPrototypes()` list rules out native instance
sources. Bare instanceable metadata and point-instancer prototype relationships
are different concepts. Keep the existing discovery order when native instances
are present, and query USD directly so activation and load changes do not need
another application cache to invalidate.
