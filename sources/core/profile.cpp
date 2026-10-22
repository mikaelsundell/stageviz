// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "profile.h"
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#ifdef _WIN32
#    include <fcntl.h>
#    include <io.h>
#    include <sys/stat.h>
#else
#    include <fcntl.h>
#    include <unistd.h>
#endif
#include <cctype>
#include <fstream>
#include <iomanip>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

namespace stageviz::profile {

namespace {

    using Clock = std::chrono::steady_clock;
    uint64_t nowNs() noexcept
    {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count());
    }
    struct SlowEvent {
        const Site* site;
        uint64_t durationNs;
        uint64_t thread;
    };
    struct State {
        std::atomic<Mode> current { Mode::Aggregate };
        std::atomic<uint64_t> slowThresholdNs { 5'000'000 };
        struct Recording {
            RecordingId id;
            std::string name;
            int fd;
            bool closeFd;
            bool syncWrites;
            std::mutex mutex;
            std::atomic<bool> active { true };
            Recording(RecordingId i, std::string n, int f, bool close, bool sync)
                : id(i)
                , name(std::move(n))
                , fd(f)
                , closeFd(close)
                , syncWrites(sync)
            {}
            ~Recording()
            {
#ifdef _WIN32
                if (closeFd && fd >= 0)
                    _close(fd);
#else
                if (closeFd && fd >= 0)
                    ::close(fd);
#endif
            }
        };
        std::mutex recordingsMutex;
        std::map<RecordingId, std::shared_ptr<Recording>> recordings;
        std::atomic<RecordingId> nextRecording { 1 };
        std::atomic<unsigned> activeRecordings { 0 };
        std::mutex sitesMutex;
        std::vector<Site*> sites;
        std::mutex traceMutex;
        std::vector<SlowEvent> ring = std::vector<SlowEvent>(4096);
        size_t cursor = 0;
        size_t count = 0;
    };
    State& state()
    {
        static auto* s = new State;
        return *s;
    }
    thread_local Scope* activeScope = nullptr;
    thread_local unsigned traceDepth = 0;
    uint64_t threadId() noexcept
    {
        return static_cast<uint64_t>(std::hash<std::thread::id> {}(std::this_thread::get_id()));
    }
    void writeOutput(int fd, const char* data, size_t size, bool syncWrites) noexcept
    {
#ifdef _WIN32
        _write(fd, data, static_cast<unsigned>(size));
        if (syncWrites)
            _commit(fd);
#else
        ::write(fd, data, size);
        if (syncWrites)
            ::fsync(fd);
#endif
    }
    void writeRecord(const char* phase, const Site* site, unsigned depth, uint64_t elapsed = 0,
                     const char* counterName = nullptr, uint64_t counterValue = 0) noexcept
    {
        auto& s = state();
        if (!s.activeRecordings.load(std::memory_order_acquire))
            return;
        std::lock_guard<std::mutex> guard(s.recordingsMutex);
        for (const auto& pair : s.recordings) {
            const auto& recording = pair.second;
            if (!recording->active.load(std::memory_order_acquire))
                continue;
            char buffer[768];
            const int n = counterName
                              ? std::snprintf(buffer, sizeof(buffer),
                                              "%llu | %llu | tid=%llu | depth=%u | COUNT | %s | value=%llu\n",
                                              static_cast<unsigned long long>(recording->id),
                                              static_cast<unsigned long long>(nowNs()),
                                              static_cast<unsigned long long>(threadId()), depth, counterName,
                                              static_cast<unsigned long long>(counterValue))
                              : std::snprintf(buffer, sizeof(buffer),
                                              "%llu | %llu | tid=%llu | depth=%u | %s | %s::%s | duration_us=%.3f\n",
                                              static_cast<unsigned long long>(recording->id),
                                              static_cast<unsigned long long>(nowNs()),
                                              static_cast<unsigned long long>(threadId()), depth, phase,
                                              site ? site->category : "recording",
                                              site ? site->name : recording->name.c_str(),
                                              static_cast<double>(elapsed) / 1000.0);
            if (n <= 0 || n >= static_cast<int>(sizeof(buffer)))
                continue;
            std::lock_guard<std::mutex> lock(recording->mutex);
            writeOutput(recording->fd, buffer, static_cast<size_t>(n), recording->syncWrites);
        }
    }

    void updateMax(std::atomic<uint64_t>& target, uint64_t value) noexcept
    {
        auto old = target.load(std::memory_order_relaxed);
        while (old < value && !target.compare_exchange_weak(old, value, std::memory_order_relaxed)) {}
    }
    void recordSlow(const Site* site, uint64_t durationNs) noexcept
    {
        auto& s = state();
        std::lock_guard<std::mutex> lock(s.traceMutex);
        if (s.ring.empty())
            return;
        s.ring[s.cursor] = { site, durationNs,
                             static_cast<uint64_t>(std::hash<std::thread::id> {}(std::this_thread::get_id())) };
        s.cursor = (s.cursor + 1) % s.ring.size();
        s.count = std::min(s.count + 1, s.ring.size());
    }
}  // namespace

Site::Site(const char* categoryValue, const char* nameValue)
    : category(categoryValue)
    , name(nameValue)
{
    auto& s = state();
    std::lock_guard<std::mutex> lock(s.sitesMutex);
    s.sites.push_back(this);
}
void
setMode(Mode value) noexcept
{
    state().current.store(value, std::memory_order_relaxed);
}
Mode
mode() noexcept
{
    return state().current.load(std::memory_order_relaxed);
}
void
setSlowThresholdUs(uint64_t us) noexcept
{
    state().slowThresholdNs.store(us * 1000, std::memory_order_relaxed);
}
void
setTraceCapacity(size_t events)
{
    auto& s = state();
    std::lock_guard<std::mutex> lock(s.traceMutex);
    s.ring.assign(events, SlowEvent {});
    s.cursor = 0;
    s.count = 0;
}
Scope::Scope(Site& site) noexcept
    : mode_(mode())
{
    auto& s = state();
    if (mode_ == Mode::Off && !s.activeRecordings.load(std::memory_order_acquire))
        return;
    site_ = &site;
    parent_ = activeScope;
    activeScope = this;
    startNs_ = nowNs();
    depth_ = traceDepth++;
    writeRecord("BEGIN", site_, depth_);
}
Scope::~Scope() noexcept
{
    if (!site_)
        return;
    const auto elapsed = nowNs() - startNs_;
    if (traceDepth)
        --traceDepth;
    writeRecord("END", site_, depth_, elapsed);
    activeScope = parent_;
    if (parent_)
        parent_->childNs_ += elapsed;
    site_->calls.fetch_add(1, std::memory_order_relaxed);
    site_->inclusiveNs.fetch_add(elapsed, std::memory_order_relaxed);
    site_->exclusiveNs.fetch_add(elapsed >= childNs_ ? elapsed - childNs_ : 0, std::memory_order_relaxed);
    updateMax(site_->maxNs, elapsed);
    if (mode_ == Mode::Trace && elapsed >= state().slowThresholdNs.load(std::memory_order_relaxed))
        recordSlow(site_, elapsed);
}

RecordingId
beginRecording(const std::string& name, const std::string& filename)
{
    if (name.empty() || !std::all_of(name.begin(), name.end(), [](unsigned char c) {
            return std::isalnum(c) || c == '_' || c == '-';
        }))
        return 0;
    const char* report = std::getenv("STAGEVIZ_PROFILE_REPORT");
    const bool toStdout = filename.empty() && !(report && *report);
    const std::string path = filename.empty() && !toStdout ? std::string(report) + "." + name + ".trace" : filename;
    std::string displayPath = "stdout";
    if (!toStdout) {
        std::error_code pathError;
        const std::filesystem::path absolutePath = std::filesystem::absolute(path, pathError);
        displayPath = pathError ? path : absolutePath.string();
    }
#ifdef _WIN32
    const int fd = toStdout ? _fileno(stdout)
                            : _open(path.c_str(), _O_CREAT | _O_WRONLY | _O_APPEND | _O_BINARY, _S_IREAD | _S_IWRITE);
#else
    const int fd = toStdout ? STDOUT_FILENO : ::open(path.c_str(), O_CREAT | O_WRONLY | O_APPEND, 0644);
#endif
    if (fd < 0) {
        std::fprintf(stderr, "Profiler: could not open recording '%s' at %s: %s\n", name.c_str(), displayPath.c_str(),
                     std::strerror(errno));
        std::fflush(stderr);
        return 0;
    }
    std::fprintf(stderr, "Profiler recording '%s' writing to %s\n", name.c_str(), displayPath.c_str());
    std::fflush(stderr);
    auto& s = state();
    const auto id = s.nextRecording.fetch_add(1, std::memory_order_relaxed);
    auto recording = std::make_shared<State::Recording>(id, name, fd, !toStdout, !toStdout);
    {
        std::lock_guard<std::mutex> lock(s.recordingsMutex);
        s.recordings.emplace(id, recording);
        s.activeRecordings.fetch_add(1, std::memory_order_release);
    }
    {
        std::lock_guard<std::mutex> lock(recording->mutex);
        char buffer[512];
        const int n = std::snprintf(buffer, sizeof(buffer),
                                    "%llu | %llu | tid=%llu | depth=0 | BEGIN | recording::%s | duration_us=0.000\n",
                                    static_cast<unsigned long long>(id), static_cast<unsigned long long>(nowNs()),
                                    static_cast<unsigned long long>(threadId()), name.c_str());
        if (n > 0 && n < static_cast<int>(sizeof(buffer))) {
            writeOutput(fd, buffer, static_cast<size_t>(n), !toStdout);
        }
    }
    return id;
}
void
endRecording(RecordingId id) noexcept
{
    auto& s = state();
    std::lock_guard<std::mutex> lock(s.recordingsMutex);
    auto it = s.recordings.find(id);
    if (it == s.recordings.end())
        return;
    auto recording = it->second;
    // End marker is written before the descriptor is closed.
    char buffer[512];
    const int n = std::snprintf(buffer, sizeof(buffer),
                                "%llu | %llu | tid=%llu | depth=0 | END | recording::%s | duration_us=0.000\n",
                                static_cast<unsigned long long>(id), static_cast<unsigned long long>(nowNs()),
                                static_cast<unsigned long long>(threadId()), recording->name.c_str());
    if (n > 0 && n < static_cast<int>(sizeof(buffer))) {
        writeOutput(recording->fd, buffer, static_cast<size_t>(n), recording->syncWrites);
    }
    recording->active.store(false, std::memory_order_release);
    s.recordings.erase(it);
    s.activeRecordings.fetch_sub(1, std::memory_order_release);
}
bool
recordingActive(RecordingId id) noexcept
{
    auto& s = state();
    std::lock_guard<std::mutex> lock(s.recordingsMutex);
    return s.recordings.find(id) != s.recordings.end();
}
void
counter(const char* name, uint64_t value) noexcept
{
    if (name)
        writeRecord("COUNT", nullptr, 0, 0, name, value);
}


void
event(const char* category, const char* name, const std::string& details)
{
    auto& s = state();
    if (!s.activeRecordings.load(std::memory_order_acquire))
        return;
    std::lock_guard<std::mutex> guard(s.recordingsMutex);
    for (const auto& pair : s.recordings) {
        const auto& recording = pair.second;
        if (!recording->active.load(std::memory_order_acquire))
            continue;
        std::ostringstream line;
        line << recording->id << " | " << nowNs() << " | tid=" << threadId() << " | depth=0 | EVENT | "
             << (category ? category : "") << "::" << (name ? name : "") << " | ";
        for (char ch : details)
            line << (ch == '\n' || ch == '\r' ? ' ' : ch);
        line << '\n';
        const std::string output = line.str();
        std::lock_guard<std::mutex> lock(recording->mutex);
        writeOutput(recording->fd, output.data(), output.size(), recording->syncWrites);
    }
}

void
reset()
{
    auto& s = state();
    std::lock_guard<std::mutex> lock(s.sitesMutex);
    for (auto* site : s.sites) {
        site->calls.store(0, std::memory_order_relaxed);
        site->inclusiveNs.store(0, std::memory_order_relaxed);
        site->exclusiveNs.store(0, std::memory_order_relaxed);
        site->maxNs.store(0, std::memory_order_relaxed);
    }
    std::lock_guard<std::mutex> traceLock(s.traceMutex);
    s.cursor = s.count = 0;
}
std::string
summary()
{
    struct Row {
        std::string name;
        uint64_t calls, inclusive, exclusive, max;
    };
    std::vector<Row> rows;
    auto& s = state();
    {
        std::lock_guard<std::mutex> lock(s.sitesMutex);
        rows.reserve(s.sites.size());
        for (const auto* site : s.sites) {
            auto count = site->calls.load(std::memory_order_relaxed);
            if (!count)
                continue;
            rows.push_back({ std::string(site->category) + "::" + site->name, count,
                             site->inclusiveNs.load(std::memory_order_relaxed),
                             site->exclusiveNs.load(std::memory_order_relaxed),
                             site->maxNs.load(std::memory_order_relaxed) });
        }
    }
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.exclusive > b.exclusive; });
    std::ostringstream out;
    out << "Stageviz profiler summary (nanosecond clock; timings include profiler overhead)\n";
    out << "Function | Calls | Inclusive ms | Exclusive ms | Avg us | Max us\n";
    out << std::fixed << std::setprecision(3);
    for (const auto& r : rows)
        out << r.name << " | " << r.calls << " | " << r.inclusive / 1e6 << " | " << r.exclusive / 1e6 << " | "
            << r.inclusive / (1e3 * r.calls) << " | " << r.max / 1e3 << '\n';
    {
        std::lock_guard<std::mutex> lock(s.traceMutex);
        if (s.count) {
            out << "\nSlow calls (oldest retained first; ring capacity " << s.ring.size() << "):\n";
            const size_t first = (s.cursor + s.ring.size() - s.count) % s.ring.size();
            for (size_t i = 0; i < s.count; ++i) {
                const auto& event = s.ring[(first + i) % s.ring.size()];
                out << event.site->category << "::" << event.site->name << " | " << event.durationNs / 1e3
                    << " us | tid=" << event.thread << '\n';
            }
        }
    }
    return out.str();
}
bool
writeSummary(const std::string& filename)
{
    std::ofstream file(filename, std::ios::binary | std::ios::trunc);
    if (!file)
        return false;
    file << summary();
    return file.good();
}
}  // namespace stageviz::profile
