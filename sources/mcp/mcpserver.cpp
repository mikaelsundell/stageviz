// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "mcpserver.h"

#include "application.h"
#include "pythoninterpreter.h"
#include "selectionlist.h"
#include "session.h"
#include "settings.h"

#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QReadLocker>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>

#include <pxr/usd/usd/attribute.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/relationship.h>
#include <pxr/usd/usd/stage.h>

#include <functional>

PXR_NAMESPACE_USING_DIRECTIVE

namespace stageviz {

namespace {

    constexpr qsizetype maxHeaderSize = 32 * 1024;
    constexpr qsizetype maxBodySize = 1024 * 1024;
    constexpr int maxPrimLimit = 500;

    QJsonObject jsonRpcError(const QJsonValue& id, int code, const QString& message)
    {
        return { { "jsonrpc", "2.0" },
                 { "id", id },
                 { "error", QJsonObject { { "code", code }, { "message", message } } } };
    }

    QJsonObject toolResult(const QString& text, bool isError = false)
    {
        QJsonObject result { { "content", QJsonArray { QJsonObject { { "type", "text" }, { "text", text } } } } };
        if (isError)
            result.insert("isError", true);
        return result;
    }

    QString jsonText(const QJsonValue& value)
    {
        if (value.isObject())
            return QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Indented));
        if (value.isArray())
            return QString::fromUtf8(QJsonDocument(value.toArray()).toJson(QJsonDocument::Indented));
        return value.toString();
    }

    QJsonObject toolDefinition(const QString& name, const QString& description, const QJsonObject& properties,
                               const QJsonArray& required, bool readOnly, bool destructive = false)
    {
        QJsonObject annotations { { "readOnlyHint", readOnly },
                                  { "destructiveHint", destructive },
                                  { "openWorldHint", false } };
        return { { "name", name },
                 { "description", description },
                 { "inputSchema", QJsonObject { { "type", "object" },
                                                { "properties", properties },
                                                { "required", required },
                                                { "additionalProperties", false } } },
                 { "annotations", annotations } };
    }

    QJsonArray availableTools()
    {
        QJsonArray tools;
        tools.append(toolDefinition("stageviz_get_stage_info", "Read information about the currently open USD stage.",
                                    {}, {}, true));
        tools.append(toolDefinition(
            "stageviz_list_prims", "List prims beneath a USD prim path. Limit depth and result count for large stages.",
            { { "path", QJsonObject { { "type", "string" }, { "description", "Absolute prim path; defaults to /." } } },
              { "maxDepth", QJsonObject { { "type", "integer" },
                                          { "minimum", 0 },
                                          { "maximum", 8 },
                                          { "description", "Maximum child depth; defaults to 1." } } },
              { "limit", QJsonObject { { "type", "integer" },
                                       { "minimum", 1 },
                                       { "maximum", maxPrimLimit },
                                       { "description", "Maximum prims returned; defaults to 100." } } } },
            {}, true));
        tools.append(toolDefinition(
            "stageviz_get_prim_info", "Read the type, composition state, attributes, and relationships for one prim.",
            { { "path", QJsonObject { { "type", "string" }, { "description", "Absolute USD prim path." } } } },
            { "path" }, true));
        tools.append(toolDefinition("stageviz_get_selection", "Return the prim paths currently selected in Stageviz.",
                                    {}, {}, true));
        tools.append(toolDefinition(
            "stageviz_set_selection", "Replace the current Stageviz selection with the supplied prim paths.",
            { { "paths",
                QJsonObject { { "type", "array" },
                              { "items", QJsonObject { { "type", "string" } } },
                              { "description", "Absolute prim paths; an empty list clears the selection." } } } },
            { "paths" }, false));
        tools.append(toolDefinition(
            "stageviz_run_python",
            "Run Python code in the embedded Stageviz interpreter. This may edit the stage directly and clears the native undo history before running.",
            { { "code", QJsonObject { { "type", "string" },
                                      { "description",
                                        "Python source code executed in Stageviz's embedded interpreter." } } } },
            { "code" }, false, true));
        return tools;
    }

    QJsonObject primDescription(const UsdPrim& prim)
    {
        int childCount = 0;
        for (const UsdPrim& child : prim.GetChildren()) {
            Q_UNUSED(child);
            ++childCount;
        }
        return { { "path", QString::fromStdString(prim.GetPath().GetString()) },
                 { "name", QString::fromStdString(prim.GetName().GetString()) },
                 { "type", QString::fromStdString(prim.GetTypeName().GetString()) },
                 { "loaded", prim.IsLoaded() },
                 { "instance", prim.IsInstance() },
                 { "instanceProxy", prim.IsInstanceProxy() },
                 { "childCount", childCount } };
    }

}  // namespace

class McpServerPrivate {
public:
    explicit McpServerPrivate(McpServer* owner)
        : q(owner)
        , server(new QTcpServer(owner))
    {
        QObject::connect(server, &QTcpServer::newConnection, q, [this]() { acceptConnections(); });
    }

    void acceptConnections()
    {
        while (server->hasPendingConnections()) {
            QTcpSocket* socket = server->nextPendingConnection();
            if (!socket)
                continue;
            socket->setReadBufferSize(maxHeaderSize + maxBodySize);
            QObject::connect(socket, &QTcpSocket::readyRead, q, [this, socket]() { readRequest(socket); });
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        }
    }

    void writeResponse(QTcpSocket* socket, int status, const QByteArray& reason, const QByteArray& body = {},
                       const QByteArray& contentType = "application/json", const QByteArray& corsOrigin = {})
    {
        if (!socket)
            return;
        QByteArray response = "HTTP/1.1 " + QByteArray::number(status) + " " + reason + "\r\n";
        response += "Content-Type: " + contentType + "\r\n";
        response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
        response += "Cache-Control: no-store\r\nConnection: close\r\n";
        if (!corsOrigin.isEmpty()) {
            response += "Access-Control-Allow-Origin: " + corsOrigin + "\r\n";
            response += "Access-Control-Allow-Methods: POST, OPTIONS\r\n";
            response += "Access-Control-Allow-Headers: Content-Type, Accept, Mcp-Protocol-Version\r\n";
            response += "Vary: Origin\r\n";
        }
        response += "\r\n";
        response += body;
        socket->write(response);
        socket->disconnectFromHost();
    }

    QByteArray errorBody(int code, const QString& message)
    {
        return QJsonDocument(jsonRpcError(QJsonValue(QJsonValue::Null), code, message)).toJson(QJsonDocument::Compact);
    }

    bool allowedHost(const QByteArray& host) const
    {
        const QByteArray port = QByteArray::number(McpServer::defaultPort);
        return host.compare("127.0.0.1:" + port, Qt::CaseInsensitive) == 0
               || host.compare("localhost:" + port, Qt::CaseInsensitive) == 0;
    }

    QByteArray allowedOrigin(const QByteArray& origin) const
    {
        if (origin.isEmpty())
            return {};
        const QUrl url(QString::fromUtf8(origin));
        if (url.scheme() != QLatin1String("http") || url.port(-1) != McpServer::defaultPort
            || (url.host() != QLatin1String("127.0.0.1") && url.host() != QLatin1String("localhost")))
            return {};
        return origin;
    }

    void readRequest(QTcpSocket* socket)
    {
        if (!socket || socket->property("mcpResponded").toBool())
            return;

        QByteArray buffer = socket->property("mcpRequestBuffer").toByteArray();
        buffer += socket->readAll();
        if (buffer.size() > maxHeaderSize + maxBodySize) {
            socket->setProperty("mcpResponded", true);
            writeResponse(socket, 413, "Payload Too Large", "Request body is too large.", "text/plain");
            return;
        }

        const qsizetype headerEnd = buffer.indexOf("\r\n\r\n");
        if (headerEnd < 0) {
            if (buffer.size() > maxHeaderSize) {
                socket->setProperty("mcpResponded", true);
                writeResponse(socket, 431, "Request Header Fields Too Large", "Request headers are too large.",
                              "text/plain");
                return;
            }
            socket->setProperty("mcpRequestBuffer", buffer);
            return;
        }

        const QList<QByteArray> lines = buffer.left(headerEnd).split('\n');
        if (lines.isEmpty()) {
            socket->setProperty("mcpResponded", true);
            writeResponse(socket, 400, "Bad Request", "Malformed request.", "text/plain");
            return;
        }
        const QList<QByteArray> requestLine = lines.first().trimmed().split(' ');
        if (requestLine.size() != 3) {
            socket->setProperty("mcpResponded", true);
            writeResponse(socket, 400, "Bad Request", "Malformed request line.", "text/plain");
            return;
        }

        QHash<QByteArray, QByteArray> headers;
        for (qsizetype i = 1; i < lines.size(); ++i) {
            const QByteArray line = lines.at(i).trimmed();
            const qsizetype separator = line.indexOf(':');
            if (separator <= 0)
                continue;
            headers.insert(line.left(separator).trimmed().toLower(), line.mid(separator + 1).trimmed());
        }

        const QByteArray origin = allowedOrigin(headers.value("origin"));
        const QByteArray host = headers.value("host");
        if (!allowedHost(host) || (!headers.value("origin").isEmpty() && origin.isEmpty())) {
            socket->setProperty("mcpResponded", true);
            writeResponse(socket, 403, "Forbidden", "Only local MCP clients are allowed.", "text/plain");
            return;
        }

        const QByteArray method = requestLine.at(0);
        const QByteArray path = requestLine.at(1);
        if (method == "OPTIONS" && path == "/mcp") {
            socket->setProperty("mcpResponded", true);
            writeResponse(socket, 204, "No Content", {}, "text/plain", origin);
            return;
        }
        if (method != "POST" || path != "/mcp") {
            socket->setProperty("mcpResponded", true);
            writeResponse(socket, 404, "Not Found", "MCP endpoint not found.", "text/plain", origin);
            return;
        }
        if (!headers.value("transfer-encoding").isEmpty()) {
            socket->setProperty("mcpResponded", true);
            writeResponse(socket, 400, "Bad Request", "Transfer-Encoding is not supported.", "text/plain", origin);
            return;
        }
        if (!headers.value("content-type").toLower().startsWith("application/json")) {
            socket->setProperty("mcpResponded", true);
            writeResponse(socket, 415, "Unsupported Media Type", "Expected application/json.", "text/plain", origin);
            return;
        }

        bool lengthOk = false;
        const qlonglong contentLength = headers.value("content-length").toLongLong(&lengthOk);
        if (!lengthOk || contentLength < 1 || contentLength > maxBodySize) {
            socket->setProperty("mcpResponded", true);
            writeResponse(socket, contentLength > maxBodySize ? 413 : 400,
                          contentLength > maxBodySize ? "Payload Too Large" : "Bad Request", "Invalid Content-Length.",
                          "text/plain", origin);
            return;
        }

        const qsizetype bodyOffset = headerEnd + 4;
        if (buffer.size() - bodyOffset < contentLength) {
            socket->setProperty("mcpRequestBuffer", buffer);
            return;
        }

        socket->setProperty("mcpResponded", true);
        const QByteArray body = buffer.mid(bodyOffset, static_cast<qsizetype>(contentLength));
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            writeResponse(socket, 400, "Bad Request", errorBody(-32700, "Invalid JSON request."), "application/json",
                          origin);
            return;
        }

        bool hasResponse = false;
        const QJsonObject response = handleMessage(document.object(), &hasResponse);
        if (!hasResponse) {
            writeResponse(socket, 202, "Accepted", {}, "application/json", origin);
            return;
        }
        writeResponse(socket, 200, "OK", QJsonDocument(response).toJson(QJsonDocument::Compact), "application/json",
                      origin);
    }

    QJsonObject handleMessage(const QJsonObject& request, bool* hasResponse)
    {
        const QJsonValue id = request.value("id");
        const bool hasId = request.contains("id");
        if (request.value("jsonrpc").toString() != QLatin1String("2.0") || !request.value("method").isString()) {
            *hasResponse = true;
            return jsonRpcError(id, -32600, "Invalid Request");
        }

        const QString method = request.value("method").toString();
        const QJsonObject params = request.value("params").toObject();
        if (method.startsWith(QLatin1String("notifications/"))) {
            *hasResponse = false;
            return {};
        }
        if (!hasId) {
            *hasResponse = true;
            return jsonRpcError(QJsonValue(QJsonValue::Null), -32600, "Requests must include an id.");
        }

        *hasResponse = true;
        QJsonValue result;
        if (method == QLatin1String("initialize")) {
            result = QJsonObject {
                { "protocolVersion", "2025-11-25" },
                { "capabilities", QJsonObject { { "tools", QJsonObject { { "listChanged", false } } } } },
                { "serverInfo",
                  QJsonObject { { "name", "Stageviz" }, { "version", QString::fromUtf8(PROJECT_VERSION) } } },
                { "instructions",
                  "Stageviz tools operate on the stage currently open in the Stageviz desktop application. "
                  "Use stageviz_run_python only when named tools are insufficient; Python can make direct edits and resets native undo history." }
            };
        }
        else if (method == QLatin1String("ping")) {
            result = QJsonObject {};
        }
        else if (method == QLatin1String("tools/list")) {
            result = QJsonObject { { "tools", availableTools() } };
        }
        else if (method == QLatin1String("tools/call")) {
            const QString name = params.value("name").toString();
            const QJsonObject arguments = params.value("arguments").toObject();
            result = callTool(name, arguments);
        }
        else {
            *hasResponse = true;
            return jsonRpcError(id, -32601, QStringLiteral("Method not found: %1").arg(method));
        }

        return { { "jsonrpc", "2.0" }, { "id", id }, { "result", result } };
    }

    QJsonObject callTool(const QString& name, const QJsonObject& args)
    {
        Application* application = Application::instance();
        Session* currentSession = application ? application->session() : nullptr;
        if (!currentSession)
            return toolResult("Stageviz session is unavailable.", true);

        if (name == QLatin1String("stageviz_get_stage_info")) {
            QJsonObject info { { "loaded", false }, { "filename", currentSession->filename() } };
            info.insert("stageUp", currentSession->stageUp() == Session::StageUp::Z ? "Z" : "Y");
            QReadLocker locker(currentSession->stageLock());
            UsdStageRefPtr stage = currentSession->stageUnsafe();
            if (!stage)
                return toolResult(jsonText(info));

            const SdfLayerHandle rootLayer = stage->GetRootLayer();
            const UsdPrim defaultPrim = stage->GetDefaultPrim();
            info.insert("loaded", true);
            info.insert("rootLayer", rootLayer ? QString::fromStdString(rootLayer->GetIdentifier()) : QString());
            info.insert("defaultPrim",
                        defaultPrim ? QString::fromStdString(defaultPrim.GetPath().GetString()) : QString());
            info.insert("layerCount", static_cast<int>(stage->GetLayerStack(true).size()));
            return toolResult(jsonText(info));
        }

        if (name == QLatin1String("stageviz_get_selection")) {
            QJsonArray paths;
            for (const SdfPath& path : currentSession->selectionList()->paths())
                paths.append(QString::fromStdString(path.GetString()));
            return toolResult(jsonText(paths));
        }

        if (name == QLatin1String("stageviz_set_selection")) {
            const QJsonArray requested = args.value("paths").toArray();
            if (requested.size() > 1000)
                return toolResult("Selection is limited to 1000 paths per request.", true);

            QList<SdfPath> paths;
            {
                QReadLocker locker(currentSession->stageLock());
                UsdStageRefPtr stage = currentSession->stageUnsafe();
                if (!stage && !requested.isEmpty())
                    return toolResult("No USD stage is currently open.", true);

                for (const QJsonValue& value : requested) {
                    if (!value.isString())
                        return toolResult("Each selection path must be a string.", true);
                    const SdfPath path(value.toString().toStdString());
                    if (!path.IsAbsolutePath() || !path.IsAbsoluteRootOrPrimPath() || !stage->GetPrimAtPath(path))
                        return toolResult(QStringLiteral("Prim does not exist: %1").arg(value.toString()), true);
                    paths.append(path);
                }
            }
            currentSession->selectionList()->updatePaths(paths);
            return toolResult(QStringLiteral("Selected %1 prim(s).").arg(paths.size()));
        }

        if (name == QLatin1String("stageviz_run_python")) {
            const QString code = args.value("code").toString();
            if (code.isEmpty())
                return toolResult("The code argument must not be empty.", true);
            if (code.toUtf8().size() > 64 * 1024)
                return toolResult("Python source is limited to 64 KiB per call.", true);
            const QString output = application->pythonInterpreter()->executeScriptWithOutput(code);
            return toolResult(output.isEmpty() ? "Script completed with no stdout or stderr output." : output);
        }

        if (name == QLatin1String("stageviz_list_prims")) {
            const QString pathText = args.value("path").toString(QStringLiteral("/"));
            const int maxDepth = qBound(0, args.value("maxDepth").toInt(1), 8);
            const int limit = qBound(1, args.value("limit").toInt(100), maxPrimLimit);
            const SdfPath path(pathText.toStdString());
            if (!path.IsAbsolutePath() || !path.IsAbsoluteRootOrPrimPath())
                return toolResult(QStringLiteral("Invalid absolute prim path: %1").arg(pathText), true);

            QJsonArray prims;
            bool truncated = false;
            QReadLocker locker(currentSession->stageLock());
            UsdStageRefPtr stage = currentSession->stageUnsafe();
            if (!stage)
                return toolResult("No USD stage is currently open.", true);

            std::function<void(const UsdPrim&, int)> appendPrim;
            appendPrim = [&](const UsdPrim& prim, int depth) {
                if (prims.size() >= limit) {
                    truncated = true;
                    return;
                }
                prims.append(primDescription(prim));
                if (depth >= maxDepth)
                    return;
                for (const UsdPrim& child : prim.GetChildren()) {
                    if (prims.size() >= limit) {
                        truncated = true;
                        return;
                    }
                    appendPrim(child, depth + 1);
                }
            };

            if (path == SdfPath::AbsoluteRootPath()) {
                for (const UsdPrim& child : stage->GetPseudoRoot().GetChildren()) {
                    if (prims.size() >= limit) {
                        truncated = true;
                        break;
                    }
                    appendPrim(child, 0);
                }
            }
            else {
                const UsdPrim root = stage->GetPrimAtPath(path);
                if (!root)
                    return toolResult(QStringLiteral("Prim does not exist: %1").arg(pathText), true);
                appendPrim(root, 0);
            }
            return toolResult(jsonText(QJsonObject { { "prims", prims }, { "truncated", truncated } }));
        }

        if (name == QLatin1String("stageviz_get_prim_info")) {
            const QString pathText = args.value("path").toString();
            const SdfPath path(pathText.toStdString());
            if (!path.IsAbsolutePath() || !path.IsAbsoluteRootOrPrimPath())
                return toolResult(QStringLiteral("Invalid absolute prim path: %1").arg(pathText), true);

            QReadLocker locker(currentSession->stageLock());
            UsdStageRefPtr stage = currentSession->stageUnsafe();
            if (!stage)
                return toolResult("No USD stage is currently open.", true);
            const UsdPrim prim = stage->GetPrimAtPath(path);
            if (!prim)
                return toolResult(QStringLiteral("Prim does not exist: %1").arg(pathText), true);

            QJsonArray attributes;
            for (const UsdAttribute& attribute : prim.GetAttributes()) {
                attributes.append(
                    QJsonObject { { "name", QString::fromStdString(attribute.GetName().GetString()) },
                                  { "type", QString::fromStdString(attribute.GetTypeName().GetAsToken().GetString()) },
                                  { "hasAuthoredValue", attribute.HasAuthoredValueOpinion() } });
            }
            QJsonArray relationships;
            for (const UsdRelationship& relationship : prim.GetRelationships())
                relationships.append(QString::fromStdString(relationship.GetName().GetString()));

            QJsonObject info = primDescription(prim);
            info.insert("active", prim.IsActive());
            info.insert("defined", prim.IsDefined());
            info.insert("abstract", prim.IsAbstract());
            info.insert("attributes", attributes);
            info.insert("relationships", relationships);
            if (prim.IsInstance())
                info.insert("prototype", QString::fromStdString(prim.GetPrototype().GetPath().GetString()));
            return toolResult(jsonText(info));
        }

        return toolResult(QStringLiteral("Unknown Stageviz tool: %1").arg(name), true);
    }

    McpServer* q = nullptr;
    QTcpServer* server = nullptr;
    QString error;
};

McpServer::McpServer(QObject* parent)
    : QObject(parent)
    , p(new McpServerPrivate(this))
{}

McpServer::~McpServer() = default;

bool
McpServer::start()
{
    if (p->server->isListening())
        return true;
    if (!p->server->listen(QHostAddress::LocalHost, defaultPort)) {
        p->error = p->server->errorString();
        Q_EMIT errorOccurred(p->error);
        return false;
    }
    p->error.clear();
    Q_EMIT runningChanged(true);
    return true;
}

void
McpServer::stop()
{
    if (!p->server->isListening())
        return;
    p->server->close();
    const auto sockets = p->server->findChildren<QTcpSocket*>();
    for (QTcpSocket* socket : sockets)
        socket->disconnectFromHost();
    Q_EMIT runningChanged(false);
}

bool
McpServer::isRunning() const
{
    return p->server->isListening();
}

QUrl
McpServer::url() const
{
    return QUrl(QStringLiteral("http://127.0.0.1:%1/mcp").arg(defaultPort));
}

QString
McpServer::errorString() const
{
    return p->error;
}

}  // namespace stageviz
