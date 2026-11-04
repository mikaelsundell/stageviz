// Copyright 2026. Licensed under the terms of the OpenUSD LICENSE.txt file.
#include "pxr/imaging/hdSt/performance.h"
#include "pxr/base/tf/getenv.h"
#include <array>
#include <atomic>
#include <chrono>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>
PXR_NAMESPACE_OPEN_SCOPE
namespace HdSt_Performance {
namespace {
struct Value { std::atomic<uint64_t> count{0}; std::atomic<int64_t> ns{0}; };
std::array<Value, MetricCount> values;
const char* names[] = {"commit_resources","resource_gc","shader_prepare",
    "resolve_bindings","codegen_setup","codegen_hash","codegen_destroy",
    "compile_link","program_hit","program_miss","batch_validate",
    "batch_rebuild","batch_rebuild_all","draw_batches","draw_items","render_pass"};
void Write(std::string const& body) {
    // Process-lifetime mutex avoids static-destruction ordering against SDK
    // registries. It is allocated only for explicitly enabled diagnostics.
    static std::mutex *mutex = new std::mutex;
    std::lock_guard<std::mutex> guard(*mutex);
    std::ofstream output(TfGetenv("HDST_PERF_LOG"), std::ios::app);
    const auto wall = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    output << "{\"wall_ms\":" << wall << "," << body << "}\n";
}
}
bool Enabled() { static const bool enabled=!TfGetenv("HDST_PERF_LOG").empty(); return enabled; }
int64_t Now() { return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
void Add(Metric metric, uint64_t count, int64_t ns) {
    if (!Enabled()) return;
    values[metric].count.fetch_add(count,std::memory_order_relaxed);
    values[metric].ns.fetch_add(ns,std::memory_order_relaxed);
}
void Flush() {
    if (!Enabled()) return;
    std::ostringstream out;
    out << "\"event\":\"render_pass\",\"metrics\":{";
    for (int i=0;i<MetricCount;++i) {
        if (i) out << ',';
        out << '"' << names[i] << "\":{\"count\":"
            << values[i].count.exchange(0,std::memory_order_relaxed)
            << ",\"ms\":" << values[i].ns.exchange(0,std::memory_order_relaxed)/1e6 << '}';
    }
    out << '}';Write(out.str());
}
void TopologyStep(const void* registry,size_t visited,size_t reclaimed,size_t remaining,
    bool pending,bool forced,double eraseNs,double destroyNs,double elapsedNs) {
    if (!Enabled()) return;
    std::ostringstream out;
    out << "\"event\":\"topology_gc\",\"registry\":\"" << registry
        << "\",\"visited\":" << visited << ",\"reclaimed\":" << reclaimed
        << ",\"remaining\":" << remaining << ",\"pending\":" << (pending?"true":"false")
        << ",\"forced_eager\":" << (forced?"true":"false")
        << ",\"erase_ms\":" << eraseNs/1e6 << ",\"destroy_ms\":" << destroyNs/1e6
        << ",\"elapsed_ms\":" << elapsedNs/1e6;
    Write(out.str());
}
}
PXR_NAMESPACE_CLOSE_SCOPE
