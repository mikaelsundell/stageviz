// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 - present Mikael Sundell

#pragma once

#include <QFile>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QThread>
#include <chrono>

namespace stageviz {

/**
 * @brief Records optional CPU phase timings for external performance runs.
 *
 * Set STAGEVIZ_PERF_LOG before startup to append JSON lines. Durations are
 * inclusive wall times, not GPU query results. No logging occurs when unset.
 */
class PerfScope {
public:
    /**
     * @brief Starts a named phase when performance logging is enabled.
     */
    explicit PerfScope(const char* name)
        : d { name, path().isEmpty() ? 0.0 : now(), path().isEmpty() ? 0 : QDateTime::currentMSecsSinceEpoch() }
    {
    }

    /**
     * @brief Appends the completed phase without changing application state.
     */
    ~PerfScope()
    {
        if (d.start == 0.0)
            return;
        const double finish = now();
        const QJsonObject event { { "phase", QString::fromLatin1(d.name) },
                                  { "start_ms", d.start },
                                  { "wall_start_ms", d.wallStart },
                                  { "duration_ms", finish - d.start },
                                  { "thread", QString::number(reinterpret_cast<quintptr>(QThread::currentThreadId()), 16) } };
        static QMutex mutex;
        QMutexLocker lock(&mutex);
        QFile output(path());
        if (output.open(QIODevice::WriteOnly | QIODevice::Append))
            output.write(QJsonDocument(event).toJson(QJsonDocument::Compact) + '\n');
    }

private:
    static const QString& path()
    {
        static const QString value = qEnvironmentVariable("STAGEVIZ_PERF_LOG");
        return value;
    }

    static double now()
    {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    struct Data {
        const char* name;
        double start;
        qint64 wallStart;
    } d;
    Q_DISABLE_COPY_MOVE(PerfScope)
};

} // namespace stageviz
