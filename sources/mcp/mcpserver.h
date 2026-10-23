// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "stageviz.h"
#include <QObject>
#include <QScopedPointer>
#include <QUrl>

class QTcpServer;
class QTcpSocket;

namespace stageviz {

class McpServerPrivate;

/**
 * @brief Hosts Stageviz's local Model Context Protocol endpoint.
 *
 * The server binds only to IPv4 loopback. It is opt-in and exposes a small
 * set of Stageviz tools over MCP Streamable HTTP.
 */
class McpServer : public QObject {
    Q_OBJECT
public:
    /**
     * @brief The TCP port used by the local MCP endpoint.
     */
    static constexpr quint16 defaultPort = 37651;

    /**
     * @brief Create an MCP server.
     * @param parent Optional QObject parent.
     */
    explicit McpServer(QObject* parent = nullptr);

    /**
     * @brief Stop the server and release its resources.
     */
    ~McpServer() override;

    /**
     * @brief Start listening on the loopback interface.
     * @return True if the server is running after this call.
     * @note If the server is already running, this returns true.
     */
    bool start();

    /**
     * @brief Stop listening and disconnect active clients.
     */
    void stop();

    /**
     * @brief Check whether the server is listening.
     */
    bool isRunning() const;

    /**
     * @brief Get the URL of the MCP endpoint.
     */
    QUrl url() const;

    /**
     * @brief Get the most recent server startup error, if any.
     */
    QString errorString() const;

Q_SIGNALS:
    /**
     * @brief Emitted when the server's running state changes.
     * @param running True when the server starts, false when it stops.
     */
    void runningChanged(bool running);

    /**
     * @brief Emitted when the server fails to start.
     * @param error Description of the startup failure.
     */
    void errorOccurred(const QString& error);

private:
    Q_DISABLE_COPY_MOVE(McpServer)
    QScopedPointer<McpServerPrivate> p;
};

}  // namespace stageviz
