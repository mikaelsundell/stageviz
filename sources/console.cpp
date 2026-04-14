// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "console.h"
#include <QMetaObject>
#include <QPointer>
#include <atomic>
#include <cstdio>
#include <thread>
#include <unistd.h>

namespace stageviz {

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

private:
    void readerLoop();

public:
    struct Data {
        int readFd = -1;
        int writeFd = -1;
        int oldStdout = -1;
        int oldStderr = -1;
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
    if (::pipe(pipeFds) != 0)
        return false;

    d.readFd = pipeFds[0];
    d.writeFd = pipeFds[1];

    d.oldStdout = ::dup(STDOUT_FILENO);
    d.oldStderr = ::dup(STDERR_FILENO);
    if (d.oldStdout < 0 || d.oldStderr < 0) {
        stop();
        return false;
    }

    std::fflush(stdout);
    std::fflush(stderr);

    if (::dup2(d.writeFd, STDOUT_FILENO) < 0 || ::dup2(d.writeFd, STDERR_FILENO) < 0) {
        stop();
        return false;
    }

    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);

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

    if (d.oldStdout >= 0) {
        ::dup2(d.oldStdout, STDOUT_FILENO);
        ::close(d.oldStdout);
        d.oldStdout = -1;
    }

    if (d.oldStderr >= 0) {
        ::dup2(d.oldStderr, STDERR_FILENO);
        ::close(d.oldStderr);
        d.oldStderr = -1;
    }

    if (d.writeFd >= 0) {
        ::close(d.writeFd);
        d.writeFd = -1;
    }

    if (d.readerThread.joinable())
        d.readerThread.join();

    if (d.readFd >= 0) {
        ::close(d.readFd);
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
        const ssize_t count = ::read(d.readFd, chunk, sizeof(chunk));
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
