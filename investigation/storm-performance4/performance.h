// Copyright 2026. Licensed under the terms of the OpenUSD LICENSE.txt file.
#ifndef PXR_IMAGING_HDST_PERFORMANCE_H
#define PXR_IMAGING_HDST_PERFORMANCE_H
#include "pxr/pxr.h"
#include <cstddef>
#include <cstdint>
PXR_NAMESPACE_OPEN_SCOPE
namespace HdSt_Performance {
enum Metric { Commit, ResourceGC, ShaderPrepare, Bindings, CodegenSetup,
    CodegenHash, CodegenDestroy, CompileLink, ProgramHit, ProgramMiss,
    BatchValidate, BatchRebuild, BatchRebuildAll, DrawBatches, DrawItems,
    RenderPass, MetricCount };
bool Enabled();
int64_t Now();
void Add(Metric metric, uint64_t count, int64_t ns = 0);
void Flush();
void TopologyStep(const void *registry, size_t visited, size_t reclaimed,
    size_t remaining, bool pending, bool forced, double eraseNs,
    double destroyNs, double elapsedNs);
class Scope {
public:
    explicit Scope(Metric metric, bool flush = false)
        : _metric(metric), _flush(flush), _begin(Enabled() ? Now() : 0) {}
    ~Scope() {
        if (_begin) { Add(_metric, 1, Now()-_begin); if (_flush) Flush(); }
    }
private:
    Metric _metric; bool _flush; int64_t _begin;
};
// Declare before the measured object, then an Arm after it. Reverse local
// destruction order measures the object's existing stack lifetime exactly.
class DestructionTimer {
public:
    explicit DestructionTimer(Metric metric) : metric(metric) {}
    ~DestructionTimer() { if (begin) Add(metric, 1, Now()-begin); }
    Metric metric; int64_t begin = 0;
};
class Arm {
public:
    explicit Arm(DestructionTimer& timer) : timer(timer) {}
    ~Arm() { if (Enabled()) timer.begin=Now(); }
private: DestructionTimer& timer;
};
}
PXR_NAMESPACE_CLOSE_SCOPE
#endif
