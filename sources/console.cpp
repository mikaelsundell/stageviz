// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "console.h"
#include <QByteArray>
#include <QMetaObject>
#include <QPointer>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <mutex>
#include <thread>

#ifdef Q_OS_WIN
#    include <fcntl.h>
#    include <io.h>
#    include <windows.h>
#else
#    include <poll.h>
#    include <unistd.h>
#endif

namespace stageviz {

namespace {

    constexpr qsizetype maxPendingCharacters = 1024 * 1024;
    constexpr qsizetype maxHistoryCharacters = 1024 * 1024;
    constexpr auto truncatedMessage = "[Stageviz console: earlier output was truncated]\n";

    enum class WaitResult { Readable, Timeout, Closed };

#ifdef Q_OS_WIN
    using fd_t = int;
    int fd_pipe(int fds[2]) { return ::_pipe(fds, 8192, _O_BINARY); }
    int fd_dup(int fd) { return ::_dup(fd); }
    int fd_dup2(int from, int to) { return ::_dup2(from, to); }
    int fd_close(int fd) { return ::_close(fd); }
    int fd_read(int fd, void* buffer, unsigned int count) { return ::_read(fd, buffer, count); }

    void set_unbuffered_stdout_stderr()
    {
        ::setvbuf(stdout, nullptr, _IONBF, 0);
        ::setvbuf(stderr, nullptr, _IONBF, 0);
    }

    bool ensure_crt_stream(FILE* stream, const char* name, const char* mode)
    {
        const int fd = ::_fileno(stream);
        if (fd >= 0)
            return true;
        return ::freopen(name, mode, stream) != nullptr;
    }

    bool ensure_stdout_stderr()
    {
        if (!ensure_crt_stream(stdout, "NUL", "w"))
            return false;
        if (!ensure_crt_stream(stderr, "NUL", "w"))
            return false;
        return true;
    }

    WaitResult fd_wait_readable(fd_t fd, int timeoutMs)
    {
        const intptr_t nativeHandle = ::_get_osfhandle(fd);
        if (nativeHandle == -1)
            return WaitResult::Closed;

        int waitedMs = 0;
        for (;;) {
            DWORD available = 0;
            if (!::PeekNamedPipe(reinterpret_cast<HANDLE>(nativeHandle), nullptr, 0, nullptr, &available, nullptr))
                return WaitResult::Closed;
            if (available > 0)
                return WaitResult::Readable;
            if (timeoutMs <= waitedMs)
                return WaitResult::Timeout;

            const int sleepMs = qMin(10, timeoutMs - waitedMs);
            ::Sleep(DWORD(sleepMs));
            waitedMs += sleepMs;
        }
    }
#else
    using fd_t = int;
    int fd_pipe(int fds[2]) { return ::pipe(fds); }
    int fd_dup(int fd) { return ::dup(fd); }
    int fd_dup2(int from, int to) { return ::dup2(from, to); }
    int fd_close(int fd) { return ::close(fd); }
    ssize_t fd_read(int fd, void* buffer, size_t count) { return ::read(fd, buffer, count); }

    void set_unbuffered_stdout_stderr()
    {
        ::setvbuf(stdout, nullptr, _IONBF, 0);
        ::setvbuf(stderr, nullptr, _IONBF, 0);
    }

    WaitResult fd_wait_readable(fd_t fd, int timeoutMs)
    {
        pollfd descriptor = { fd, POLLIN, 0 };
        int result = 0;
        do {
            result = ::poll(&descriptor, 1, timeoutMs);
        } while (result < 0 && errno == EINTR);

        if (result == 0)
            return WaitResult::Timeout;
        if (result < 0)
            return WaitResult::Closed;
        if (descriptor.revents & POLLIN)
            return WaitResult::Readable;
        return WaitResult::Closed;
    }
#endif

    bool appendBounded(QString& destination, const QString& text, qsizetype maximumCharacters)
    {
        destination += text;
        const qsizetype excess = destination.size() - maximumCharacters;
        if (excess > 0) {
            destination.remove(0, excess);
            return true;
        }
        return false;
    }

    QString fdState(const char* name, fd_t fd)
    {
        return QStringLiteral("%1=%2").arg(QString::fromLatin1(name)).arg(fd);
    }

}  // namespace

class ConsolePrivate : public QObject {
    Q_OBJECT
public:
    ConsolePrivate();
    ~ConsolePrivate();

    void init();
    bool start();
    void stop();
    bool isRunning() const;
    QString text() const;
    QStringList lines() const;

    void readerLoop();
    void queueText(const QString& text);
    void drainPending();

    struct Data {
        fd_t readFd = -1;
        fd_t writeFd = -1;
        fd_t oldStdout = -1;
        fd_t oldStderr = -1;
        std::atomic_bool running = false;
        QString buffer;
        QString pending;
        std::mutex pendingMutex;
        bool drainScheduled = false;
        bool pendingTruncated = false;
        std::thread readerThread;
        QPointer<Console> console;
    };
    Data d;
};

ConsolePrivate::ConsolePrivate() {}

ConsolePrivate::~ConsolePrivate() { stop(); }

void
ConsolePrivate::init()
{}

bool
ConsolePrivate::start()
{
    if (d.running) {
        return true;
    }
    int pipeFds[2] = { -1, -1 };
    if (fd_pipe(pipeFds) != 0) {
        return false;
    }
    d.readFd = pipeFds[0];
    d.writeFd = pipeFds[1];

#ifdef Q_OS_WIN
    if (!ensure_stdout_stderr()) {
        stop();
        return false;
    }

    const int stdoutFd = ::_fileno(stdout);
    const int stderrFd = ::_fileno(stderr);
    d.oldStdout = fd_dup(stdoutFd);
    d.oldStderr = fd_dup(stderrFd);
#else
    d.oldStdout = fd_dup(STDOUT_FILENO);
    d.oldStderr = fd_dup(STDERR_FILENO);
#endif

    if (d.oldStdout < 0 || d.oldStderr < 0) {
        stop();
        return false;
    }

    std::fflush(stdout);
    std::fflush(stderr);

#ifdef Q_OS_WIN
    if (fd_dup2(d.writeFd, ::_fileno(stdout)) < 0) {
        stop();
        return false;
    }

    if (fd_dup2(d.writeFd, ::_fileno(stderr)) < 0) {
        stop();
        return false;
    }
#else
    if (fd_dup2(d.writeFd, STDOUT_FILENO) < 0) {
        stop();
        return false;
    }

    if (fd_dup2(d.writeFd, STDERR_FILENO) < 0) {
        stop();
        return false;
    }
#endif

    set_unbuffered_stdout_stderr();
    d.running = true;
    d.readerThread = std::thread([this]() { readerLoop(); });
    return true;
}

void
ConsolePrivate::stop()
{
    if (!d.running && d.readFd < 0 && d.writeFd < 0 && d.oldStdout < 0 && d.oldStderr < 0) {
        return;
    }

    std::fflush(stdout);
    std::fflush(stderr);

#ifdef Q_OS_WIN
    if (d.oldStdout >= 0) {
        fd_dup2(d.oldStdout, ::_fileno(stdout));
        fd_close(d.oldStdout);
        d.oldStdout = -1;
    }
    if (d.oldStderr >= 0) {
        fd_dup2(d.oldStderr, ::_fileno(stderr));
        fd_close(d.oldStderr);
        d.oldStderr = -1;
    }
#else
    if (d.oldStdout >= 0) {
        fd_dup2(d.oldStdout, STDOUT_FILENO);
        fd_close(d.oldStdout);
        d.oldStdout = -1;
    }
    if (d.oldStderr >= 0) {
        fd_dup2(d.oldStderr, STDERR_FILENO);
        fd_close(d.oldStderr);
        d.oldStderr = -1;
    }
#endif

    if (d.writeFd >= 0) {
        fd_close(d.writeFd);
        d.writeFd = -1;
    }

    // The reader uses a bounded wait, so it can stop even when a library has
    // retained a duplicate of stdout or stderr and the pipe never reaches EOF.
    d.running = false;

    if (d.readerThread.joinable()) {
        d.readerThread.join();
    }

    if (d.readFd >= 0) {
        fd_close(d.readFd);
        d.readFd = -1;
    }
}

bool
ConsolePrivate::isRunning() const
{
    return d.running;
}

QString
ConsolePrivate::text() const
{
    return d.buffer;
}

QStringList
ConsolePrivate::lines() const
{
    return d.buffer.split('\n', Qt::KeepEmptyParts);
}

void
ConsolePrivate::readerLoop()
{
    char chunk[4096];
    auto readChunk = [&]() {
#ifdef Q_OS_WIN
        const int count = fd_read(d.readFd, chunk, unsigned(sizeof(chunk)));
#else
        const ssize_t count = fd_read(d.readFd, chunk, sizeof(chunk));
#endif
        if (count > 0) {
            queueText(QString::fromLocal8Bit(chunk, int(count)));
            return true;
        }
        return false;
    };

    while (d.running) {
        const WaitResult result = fd_wait_readable(d.readFd, 100);
        if (result == WaitResult::Closed)
            break;
        if (result == WaitResult::Readable && !readChunk())
            break;
    }

    // Preserve anything already buffered in the pipe at shutdown without
    // waiting for duplicated writer descriptors to close.
    while (fd_wait_readable(d.readFd, 0) == WaitResult::Readable) {
        if (!readChunk())
            break;
    }
}

void
ConsolePrivate::queueText(const QString& text)
{
    bool scheduleDrain = false;
    {
        std::lock_guard<std::mutex> guard(d.pendingMutex);
        if (appendBounded(d.pending, text, maxPendingCharacters))
            d.pendingTruncated = true;

        if (!d.drainScheduled) {
            d.drainScheduled = true;
            scheduleDrain = true;
        }
    }

    if (!scheduleDrain)
        return;

    const bool invoked = QMetaObject::invokeMethod(this, [this]() { drainPending(); }, Qt::QueuedConnection);
    if (!invoked) {
        std::lock_guard<std::mutex> guard(d.pendingMutex);
        d.drainScheduled = false;
    }
}

void
ConsolePrivate::drainPending()
{
    QString text;
    bool truncated = false;
    {
        std::lock_guard<std::mutex> guard(d.pendingMutex);
        text.swap(d.pending);
        truncated = d.pendingTruncated;
        d.pendingTruncated = false;
        d.drainScheduled = false;
    }

    if (truncated)
        text.prepend(QString::fromLatin1(truncatedMessage));
    if (text.isEmpty())
        return;

    appendBounded(d.buffer, text, maxHistoryCharacters);
    if (d.console)
        Q_EMIT d.console->textAppended(text);
}

Console::Console(QObject* parent)
    : QObject(parent)
    , p(new ConsolePrivate)
{
    p->init();
    p->d.console = this;
}

Console::~Console() {}

bool
Console::start()
{
    return p->start();
}

void
Console::stop()
{
    p->stop();
}

bool
Console::isRunning() const
{
    return p->isRunning();
}

QString
Console::text() const
{
    return p->text();
}

QStringList
Console::lines() const
{
    return p->lines();
}

}  // namespace stageviz

#include "console.moc"
