// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "console.h"
#include <QMetaObject>
#include <QPointer>
#include <atomic>
#include <cstdio>
#include <thread>

#ifdef Q_OS_WIN
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

public:
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

ConsolePrivate::ConsolePrivate() {}

ConsolePrivate::~ConsolePrivate() { stop(); }

void
ConsolePrivate::init()
{}

bool
ConsolePrivate::start()
{
    if (d.running)
        return true;
    int pipeFds[2] = { -1, -1 };
    if (fd_pipe(pipeFds) != 0)
        return false;
    d.readFd = pipeFds[0];
    d.writeFd = pipeFds[1];
#ifdef Q_OS_WIN
    d.oldStdout = fd_dup(_fileno(stdout));
    d.oldStderr = fd_dup(_fileno(stderr));
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
    if (fd_dup2(d.writeFd, _fileno(stdout)) < 0 || fd_dup2(d.writeFd, _fileno(stderr)) < 0) {
        stop();
        return false;
    }
#else
    if (fd_dup2(d.writeFd, STDOUT_FILENO) < 0 || fd_dup2(d.writeFd, STDERR_FILENO) < 0) {
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
    if (!d.running && d.readFd < 0 && d.writeFd < 0 && d.oldStdout < 0 && d.oldStderr < 0)
        return;

    d.running = false;

    std::fflush(stdout);
    std::fflush(stderr);

#ifdef Q_OS_WIN
    if (d.oldStdout >= 0) {
        fd_dup2(d.oldStdout, _fileno(stdout));
        fd_close(d.oldStdout);
        d.oldStdout = -1;
    }
    if (d.oldStderr >= 0) {
        fd_dup2(d.oldStderr, _fileno(stderr));
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
    if (d.readerThread.joinable())
        d.readerThread.join();

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

    for (;;) {
#ifdef Q_OS_WIN
        const int count = fd_read(d.readFd, chunk, unsigned(sizeof(chunk)));
#else
        const ssize_t count = fd_read(d.readFd, chunk, sizeof(chunk));
#endif
        if (count > 0) {
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
            }
            continue;
        }
        if (count == 0)
            break;
        break;
    }
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
