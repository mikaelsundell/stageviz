// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz
//
// Debug lock helpers for QReadWriteLock.
//
// Usage:
//
//   #include "debuglock.h"
//
//   void foo()
//   {
//       STAGE_READ_LOCKER(locker);
//       // ...
//   }
//
//   void bar()
//   {
//       STAGE_WRITE_LOCKER(locker);
//       // ...
//   }
//
// By default this uses plain QReadLocker / QWriteLocker.
//
// To enable tracing, define:
//
//   #define STAGEVIZ_TRACE_LOCKS 1
//
// before including this header, or add it to your build defines.
//
// Optional threshold tuning:
//
//   #define STAGEVIZ_TRACE_LOCKS_WAIT_MS  1.0
//   #define STAGEVIZ_TRACE_LOCKS_HOLD_MS  2.0
//
// The header assumes CommandDispatcher::stageLock() returns QReadWriteLock*.

#pragma once

#include <QDebug>
#include <QElapsedTimer>
#include <QReadWriteLock>
#include <QThread>

#ifndef STAGEVIZ_TRACE_LOCKS
#    define STAGEVIZ_TRACE_LOCKS 0
#endif

#ifndef STAGEVIZ_TRACE_LOCKS_WAIT_MS
#    define STAGEVIZ_TRACE_LOCKS_WAIT_MS 1.0
#endif

#ifndef STAGEVIZ_TRACE_LOCKS_HOLD_MS
#    define STAGEVIZ_TRACE_LOCKS_HOLD_MS 2.0
#endif

namespace stageviz::debug {

inline quintptr
currentThreadId()
{
    return reinterpret_cast<quintptr>(QThread::currentThreadId());
}

inline qint64
msToNs(double ms)
{
    return static_cast<qint64>(ms * 1000000.0);
}

class DebugReadLocker {
public:
    DebugReadLocker(QReadWriteLock* lock, const char* name, const char* file, int line, const char* function)
        : m_lock(lock)
        , m_name(name ? name : "")
        , m_file(file ? file : "")
        , m_line(line)
        , m_function(function ? function : "")
    {
        if (!m_lock)
            return;

        m_waitTimer.start();
        m_lock->lockForRead();
        m_waitNs = m_waitTimer.nsecsElapsed();
        m_holdTimer.start();

        if (m_waitNs >= waitThresholdNs()) {
            qDebug().noquote() << QStringLiteral("LOCK_ACQ  READ  %1  tid=%2  wait=%3 ms  %4:%5  %6")
                                      .arg(QLatin1String(m_name))
                                      .arg(currentThreadId())
                                      .arg(double(m_waitNs) / 1000000.0, 0, 'f', 3)
                                      .arg(QLatin1String(m_file))
                                      .arg(m_line)
                                      .arg(QLatin1String(m_function));
        }
    }

    ~DebugReadLocker() { unlock(); }

    DebugReadLocker(const DebugReadLocker&) = delete;
    DebugReadLocker& operator=(const DebugReadLocker&) = delete;

    DebugReadLocker(DebugReadLocker&& other) noexcept { moveFrom(std::move(other)); }

    DebugReadLocker& operator=(DebugReadLocker&& other) noexcept
    {
        if (this != &other) {
            unlock();
            moveFrom(std::move(other));
        }
        return *this;
    }

    void unlock()
    {
        if (!m_lock || !m_locked)
            return;

        const qint64 holdNs = m_holdTimer.nsecsElapsed();

        if (holdNs >= holdThresholdNs()) {
            qDebug().noquote() << QStringLiteral("UNLOCK    READ  %1  tid=%2  hold=%3 ms  %4:%5  %6")
                                      .arg(QLatin1String(m_name))
                                      .arg(currentThreadId())
                                      .arg(double(holdNs) / 1000000.0, 0, 'f', 3)
                                      .arg(QLatin1String(m_file))
                                      .arg(m_line)
                                      .arg(QLatin1String(m_function));
        }

        m_lock->unlock();
        m_locked = false;
    }

private:
    static qint64 waitThresholdNs()
    {
        static const qint64 value = msToNs(STAGEVIZ_TRACE_LOCKS_WAIT_MS);
        return value;
    }

    static qint64 holdThresholdNs()
    {
        static const qint64 value = msToNs(STAGEVIZ_TRACE_LOCKS_HOLD_MS);
        return value;
    }

    void moveFrom(DebugReadLocker&& other) noexcept
    {
        m_lock = other.m_lock;
        m_name = other.m_name;
        m_file = other.m_file;
        m_line = other.m_line;
        m_function = other.m_function;
        m_waitTimer = other.m_waitTimer;
        m_holdTimer = other.m_holdTimer;
        m_waitNs = other.m_waitNs;
        m_locked = other.m_locked;

        other.m_lock = nullptr;
        other.m_locked = false;
    }

    QReadWriteLock* m_lock = nullptr;
    const char* m_name = "";
    const char* m_file = "";
    int m_line = 0;
    const char* m_function = "";
    QElapsedTimer m_waitTimer;
    QElapsedTimer m_holdTimer;
    qint64 m_waitNs = 0;
    bool m_locked = true;
};

class DebugWriteLocker {
public:
    DebugWriteLocker(QReadWriteLock* lock, const char* name, const char* file, int line, const char* function)
        : m_lock(lock)
        , m_name(name ? name : "")
        , m_file(file ? file : "")
        , m_line(line)
        , m_function(function ? function : "")
    {
        if (!m_lock)
            return;

        m_waitTimer.start();
        m_lock->lockForWrite();
        m_waitNs = m_waitTimer.nsecsElapsed();
        m_holdTimer.start();

        if (m_waitNs >= waitThresholdNs()) {
            qDebug().noquote() << QStringLiteral("LOCK_ACQ  WRITE %1  tid=%2  wait=%3 ms  %4:%5  %6")
                                      .arg(QLatin1String(m_name))
                                      .arg(currentThreadId())
                                      .arg(double(m_waitNs) / 1000000.0, 0, 'f', 3)
                                      .arg(QLatin1String(m_file))
                                      .arg(m_line)
                                      .arg(QLatin1String(m_function));
        }
    }

    ~DebugWriteLocker() { unlock(); }

    DebugWriteLocker(const DebugWriteLocker&) = delete;
    DebugWriteLocker& operator=(const DebugWriteLocker&) = delete;

    DebugWriteLocker(DebugWriteLocker&& other) noexcept { moveFrom(std::move(other)); }

    DebugWriteLocker& operator=(DebugWriteLocker&& other) noexcept
    {
        if (this != &other) {
            unlock();
            moveFrom(std::move(other));
        }
        return *this;
    }

    void unlock()
    {
        if (!m_lock || !m_locked)
            return;

        const qint64 holdNs = m_holdTimer.nsecsElapsed();

        if (holdNs >= holdThresholdNs()) {
            qDebug().noquote() << QStringLiteral("UNLOCK    WRITE %1  tid=%2  hold=%3 ms  %4:%5  %6")
                                      .arg(QLatin1String(m_name))
                                      .arg(currentThreadId())
                                      .arg(double(holdNs) / 1000000.0, 0, 'f', 3)
                                      .arg(QLatin1String(m_file))
                                      .arg(m_line)
                                      .arg(QLatin1String(m_function));
        }

        m_lock->unlock();
        m_locked = false;
    }

private:
    static qint64 waitThresholdNs()
    {
        static const qint64 value = msToNs(STAGEVIZ_TRACE_LOCKS_WAIT_MS);
        return value;
    }

    static qint64 holdThresholdNs()
    {
        static const qint64 value = msToNs(STAGEVIZ_TRACE_LOCKS_HOLD_MS);
        return value;
    }

    void moveFrom(DebugWriteLocker&& other) noexcept
    {
        m_lock = other.m_lock;
        m_name = other.m_name;
        m_file = other.m_file;
        m_line = other.m_line;
        m_function = other.m_function;
        m_waitTimer = other.m_waitTimer;
        m_holdTimer = other.m_holdTimer;
        m_waitNs = other.m_waitNs;
        m_locked = other.m_locked;

        other.m_lock = nullptr;
        other.m_locked = false;
    }

    QReadWriteLock* m_lock = nullptr;
    const char* m_name = "";
    const char* m_file = "";
    int m_line = 0;
    const char* m_function = "";
    QElapsedTimer m_waitTimer;
    QElapsedTimer m_holdTimer;
    qint64 m_waitNs = 0;
    bool m_locked = true;
};

}  // namespace stageviz::debug

#if STAGEVIZ_TRACE_LOCKS

#    define STAGEVIZ_READ_LOCKER(var, lock, name) \
        ::stageviz::debug::DebugReadLocker var((lock), (name), __FILE__, __LINE__, Q_FUNC_INFO)

#    define STAGEVIZ_WRITE_LOCKER(var, lock, name) \
        ::stageviz::debug::DebugWriteLocker var((lock), (name), __FILE__, __LINE__, Q_FUNC_INFO)

#else

#    define STAGEVIZ_READ_LOCKER(var, lock, name) QReadLocker var((lock))

#    define STAGEVIZ_WRITE_LOCKER(var, lock, name) QWriteLocker var((lock))

#endif

#define READ_LOCKER(var, lock, name) STAGEVIZ_READ_LOCKER(var, lock, name)

#define WRITE_LOCKER(var, lock, name) STAGEVIZ_WRITE_LOCKER(var, lock, name)
