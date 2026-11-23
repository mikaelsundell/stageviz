# Performance references

The Stageviz benchmark reads and writes JSON baselines under
`stageviz-viewport/` in this directory. See the [Stageviz viewport benchmark
workflow](../../CTest/benchmark/README.md#stageviz-viewport-baseline) for the
comparison and save prompt.

Compare machines using the same Stageviz build and synthetic fixture. To
measure a software change, run both revisions on the same machine. When
replacing a reference machine, run the old revision on the new one to
separate the hardware change from later software changes.

Each baseline records the hardware and software profile, build configuration,
source revision, binary and fixture checksums, workload settings, and sample
counts.

Publish measurements from generated fixtures or public test assets approved
for sharing. Keep installation IDs and their hashes, private input data,
local paths, screenshots, and raw logs out of the repository. The automatic
test suite's API timing baselines remain local in each build's
`test-baselines/` directory.
