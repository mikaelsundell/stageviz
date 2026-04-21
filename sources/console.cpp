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
#include <thread>

#ifdef Q_OS_WIN
#    include <windows.h>
#    include <fcntl.h>
#    include <io.h>
#else
#    include <unistd.h>
#endif

namespace stageviz {

namespace {

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
#endif

void debugLog(const QString& message)
{
    const QByteArray utf8 = QStringLiteral("[Console] %1\n").arg(message).toUtf8();
#ifdef Q_OS_WIN
    ::OutputDebugStringA(utf8.constData());
#else
    std::fwrite(utf8.constData(), 1, size_t(utf8.size()), stderr);
    std::fflush(stderr);
#endif
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

    struct Data {
        fd_t readFd = -1;
        fd_t writeFd = -1;
        fd_t oldStdout = -1;
        fd_t oldStderr = -1;
        std::atomic_bool running = false;
        QString buffer;
        std::thread readerThread;
        QPointer<Console> console;
    };
    Data d;
};

ConsolePrivate::ConsolePrivate()
{
    debugLog(QStringLiteral("ConsolePrivate ctor"));
}

ConsolePrivate::~ConsolePrivate()
{
    debugLog(QStringLiteral("ConsolePrivate dtor"));
    stop();
}

void
ConsolePrivate::init()
{
    debugLog(QStringLiteral("init()"));
}

bool
ConsolePrivate::start()
{
    debugLog(QStringLiteral("start() called"));
    debugLog(QStringLiteral("before start: running=%1, %2, %3, %4, %5")
                 .arg(d.running.load() ? QStringLiteral("true") : QStringLiteral("false"))
                 .arg(fdState("readFd", d.readFd))
                 .arg(fdState("writeFd", d.writeFd))
                 .arg(fdState("oldStdout", d.oldStdout))
                 .arg(fdState("oldStderr", d.oldStderr)));

    if (d.running) {
        debugLog(QStringLiteral("start() skipped, already running"));
        return true;
    }

    int pipeFds[2] = { -1, -1 };
    if (fd_pipe(pipeFds) != 0) {
        debugLog(QStringLiteral("fd_pipe() failed"));
        return false;
    }

    d.readFd = pipeFds[0];
    d.writeFd = pipeFds[1];

    debugLog(QStringLiteral("pipe created: read=%1 write=%2").arg(d.readFd).arg(d.writeFd));

#ifdef Q_OS_WIN
    if (!ensure_stdout_stderr()) {
        debugLog(QStringLiteral("failed to initialize CRT stdout/stderr"));
        stop();
        return false;
    }

    const int stdoutFd = ::_fileno(stdout);
    const int stderrFd = ::_fileno(stderr);
    debugLog(QStringLiteral("windows std handles after ensure: stdout=%1 stderr=%2")
                 .arg(stdoutFd)
                 .arg(stderrFd));

    d.oldStdout = fd_dup(stdoutFd);
    d.oldStderr = fd_dup(stderrFd);
#else
    d.oldStdout = fd_dup(STDOUT_FILENO);
    d.oldStderr = fd_dup(STDERR_FILENO);
#endif

    debugLog(QStringLiteral("dup old streams: oldStdout=%1 oldStderr=%2")
                 .arg(d.oldStdout)
                 .arg(d.oldStderr));

    if (d.oldStdout < 0 || d.oldStderr < 0) {
        debugLog(QStringLiteral("failed to duplicate stdout/stderr"));
        stop();
        return false;
    }

    std::fflush(stdout);
    std::fflush(stderr);
    debugLog(QStringLiteral("flushed stdout/stderr"));

#ifdef Q_OS_WIN
    if (fd_dup2(d.writeFd, ::_fileno(stdout)) < 0) {
        debugLog(QStringLiteral("dup2 to stdout failed"));
        stop();
        return false;
    }
    debugLog(QStringLiteral("dup2 to stdout ok"));

    if (fd_dup2(d.writeFd, ::_fileno(stderr)) < 0) {
        debugLog(QStringLiteral("dup2 to stderr failed"));
        stop();
        return false;
    }
    debugLog(QStringLiteral("dup2 to stderr ok"));
#else
    if (fd_dup2(d.writeFd, STDOUT_FILENO) < 0) {
        debugLog(QStringLiteral("dup2 to stdout failed"));
        stop();
        return false;
    }
    debugLog(QStringLiteral("dup2 to stdout ok"));

    if (fd_dup2(d.writeFd, STDERR_FILENO) < 0) {
        debugLog(QStringLiteral("dup2 to stderr failed"));
        stop();
        return false;
    }
    debugLog(QStringLiteral("dup2 to stderr ok"));
#endif

    set_unbuffered_stdout_stderr();
    debugLog(QStringLiteral("set_unbuffered_stdout_stderr() done"));

    d.running = true;
    debugLog(QStringLiteral("running=true, starting reader thread"));

    d.readerThread = std::thread([this]() { readerLoop(); });

    debugLog(QStringLiteral("reader thread started"));
    return true;
}

void
ConsolePrivate::stop()
{
    debugLog(QStringLiteral("stop() called"));
    debugLog(QStringLiteral("before stop: running=%1, %2, %3, %4, %5")
                 .arg(d.running.load() ? QStringLiteral("true") : QStringLiteral("false"))
                 .arg(fdState("readFd", d.readFd))
                 .arg(fdState("writeFd", d.writeFd))
                 .arg(fdState("oldStdout", d.oldStdout))
                 .arg(fdState("oldStderr", d.oldStderr)));

    if (!d.running && d.readFd < 0 && d.writeFd < 0 && d.oldStdout < 0 && d.oldStderr < 0) {
        debugLog(QStringLiteral("stop() skipped, already fully stopped"));
        return;
    }

    d.running = false;

    std::fflush(stdout);
    std::fflush(stderr);
    debugLog(QStringLiteral("flushed stdout/stderr in stop()"));

#ifdef Q_OS_WIN
    if (d.oldStdout >= 0) {
        debugLog(QStringLiteral("restoring stdout from fd %1").arg(d.oldStdout));
        fd_dup2(d.oldStdout, ::_fileno(stdout));
        fd_close(d.oldStdout);
        d.oldStdout = -1;
    }
    if (d.oldStderr >= 0) {
        debugLog(QStringLiteral("restoring stderr from fd %1").arg(d.oldStderr));
        fd_dup2(d.oldStderr, ::_fileno(stderr));
        fd_close(d.oldStderr);
        d.oldStderr = -1;
    }
#else
    if (d.oldStdout >= 0) {
        debugLog(QStringLiteral("restoring stdout from fd %1").arg(d.oldStdout));
        fd_dup2(d.oldStdout, STDOUT_FILENO);
        fd_close(d.oldStdout);
        d.oldStdout = -1;
    }
    if (d.oldStderr >= 0) {
        debugLog(QStringLiteral("restoring stderr from fd %1").arg(d.oldStderr));
        fd_dup2(d.oldStderr, STDERR_FILENO);
        fd_close(d.oldStderr);
        d.oldStderr = -1;
    }
#endif

    if (d.writeFd >= 0) {
        debugLog(QStringLiteral("closing writeFd %1").arg(d.writeFd));
        fd_close(d.writeFd);
        d.writeFd = -1;
    }

    if (d.readerThread.joinable()) {
        debugLog(QStringLiteral("joining reader thread"));
        d.readerThread.join();
        debugLog(QStringLiteral("reader thread joined"));
    }

    if (d.readFd >= 0) {
        debugLog(QStringLiteral("closing readFd %1").arg(d.readFd));
        fd_close(d.readFd);
        d.readFd = -1;
    }

    debugLog(QStringLiteral("stop() done"));
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
    debugLog(QStringLiteral("readerLoop() entered"));
    char chunk[4096];

    for (;;) {
#ifdef Q_OS_WIN
        const int count = fd_read(d.readFd, chunk, unsigned(sizeof(chunk)));
#else
        const ssize_t count = fd_read(d.readFd, chunk, sizeof(chunk));
#endif

        if (count > 0) {
            debugLog(QStringLiteral("readerLoop() read %1 bytes").arg(count));

            const QString text = QString::fromLocal8Bit(chunk, int(count));

            if (d.console) {
                QMetaObject::invokeMethod(
                    d.console.data(),
                    [this, text]() {
                        d.buffer += text;
                        if (d.console)
                            Q_EMIT d.console->textAppended(text);
                    },
                    Qt::QueuedConnection);
            } else {
                debugLog(QStringLiteral("readerLoop() has no console instance"));
            }
            continue;
        }

        if (count == 0) {
            debugLog(QStringLiteral("readerLoop() EOF"));
            break;
        }

        debugLog(QStringLiteral("readerLoop() read error, count=%1 errno=%2").arg(count).arg(errno));
        break;
    }

    debugLog(QStringLiteral("readerLoop() exiting"));
}

Console::Console(QObject* parent)
    : QObject(parent)
    , p(new ConsolePrivate)
{
    p->init();
    p->d.console = this;
    debugLog(QStringLiteral("Console QObject created"));
}

Console::~Console()
{
    debugLog(QStringLiteral("Console QObject destroyed"));
}

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