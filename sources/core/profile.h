// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>

namespace stageviz::profile {

/** Controls instrumentation outside explicitly recorded diagnostic sessions. */
enum class Mode : uint8_t { Off, Aggregate, Trace };

/** Static registration and accumulated timings for one instrumented source location. */
struct Site {
    const char* category;
    const char* name;
    std::atomic<uint64_t> calls { 0 };
    std::atomic<uint64_t> inclusiveNs { 0 };
    std::atomic<uint64_t> exclusiveNs { 0 };
    std::atomic<uint64_t> maxNs { 0 };
    Site(const char* category, const char* name);
    Site(const Site&) = delete;
    Site& operator=(const Site&) = delete;
};

void
setMode(Mode mode) noexcept;
Mode
mode() noexcept;
void
setSlowThresholdUs(uint64_t microseconds) noexcept;
void
setTraceCapacity(size_t events);

/**
 * Starts an independently named diagnostic recording. Multiple recordings may
 * overlap, and each receives events from all instrumented threads. Returns a
 * nonzero handle on success, or zero if the output file cannot be opened.
 *
 * Each recording writes BEGIN/END scope events and numeric counters. If no
 * filename or STAGEVIZ_PROFILE_REPORT is supplied, output goes to stdout. The
 * resolved destination is printed to stderr when recording starts. A non-empty
 * filename is used as-is. File recordings are immediately flushed.
 * Use endRecording(handle) only after any asynchronous work has completed.
 * Names should contain only letters, digits, '_' or '-'.
 */
using RecordingId = uint64_t;
RecordingId
beginRecording(const std::string& name, const std::string& filename = {});
void
endRecording(RecordingId id) noexcept;
bool
recordingActive(RecordingId id) noexcept;
/** Writes a numeric event into every currently active recording. */
void
counter(const char* name, uint64_t value) noexcept;
/** Records structured diagnostic text only while a named recording is active. */
void
event(const char* category, const char* name, const std::string& details);

/** Reset aggregate statistics only when no measured scopes are running. */
void
reset();
/** Aggregate report; preferably call when instrumented work is idle. */
std::string
summary();
bool
writeSummary(const std::string& filename);

/** RAII timer for a registered source location; supports nested scopes. */
class Scope final {
public:
    explicit Scope(Site& site) noexcept;
    ~Scope() noexcept;
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;

private:
    Site* site_ = nullptr;
    uint64_t startNs_ = 0;
    uint64_t childNs_ = 0;
    Scope* parent_ = nullptr;
    Mode mode_ = Mode::Off;
    unsigned depth_ = 0;
};

}  // namespace stageviz::profile

#define SV_PROFILE_JOIN2(a, b) a##b
#define SV_PROFILE_JOIN(a, b) SV_PROFILE_JOIN2(a, b)
#define SV_PROFILE_SCOPE(category, name) SV_PROFILE_SCOPE_IMPL(category, name, __COUNTER__)
#define SV_PROFILE_SCOPE_IMPL(category, name, id)                                               \
    static ::stageviz::profile::Site SV_PROFILE_JOIN(_sv_profile_site_, id) { category, name }; \
    ::stageviz::profile::Scope SV_PROFILE_JOIN(_sv_profile_scope_, id) { SV_PROFILE_JOIN(_sv_profile_site_, id) }
#define STAGEVIZ_PROFILE_SCOPE(category, name) SV_PROFILE_SCOPE(category, name)
