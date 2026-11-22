# Performance references

These records preserve measurements on reference hardware so they can be
compared after a hardware upgrade or after the original machine is retired.
The companion JSON is the source for the numbers in each Markdown report.

- [Stageviz 0.6.0 Windows reference A](0.6.0.md)

The Stageviz-only benchmark runners read and write JSON baselines under
`stageviz-viewport/` in this directory. See the [Stageviz viewport benchmark
workflow](../../CTest/benchmark/README.md#stageviz-viewport-baseline) for the
comparison and save prompt.

Compare machines using the same Stageviz build and synthetic fixture. To
measure a software change, run both revisions on the same machine. When
replacing a reference machine, run the old revision on the new one to
separate the hardware change from later software changes.

Record the reference label, hardware and software profile, build configuration,
source revision, fixture checksum, workload settings, and sample counts.
Use several serial runs and verify their viewport dimensions agree. Include
source-state and binary hashes when measuring a working-tree build.

Publish measurements from generated fixtures or public test assets approved
for sharing. Keep installation IDs and their hashes, private input data,
local paths, screenshots, and raw logs out of the repository. The automatic
test suite's API timing baselines remain local in each build's
`test-baselines/` directory.

See the [viewer swap benchmark](../../CTest/benchmark/README.md#stageviz-and-usdview-viewport-swaps)
for the reproduction command and timing definitions.
