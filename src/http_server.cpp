// SPDX-FileCopyrightText: Copyright 2026 Resident Evil Revelations Custom Server Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "http_server.h"

#include <QPointer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>

#include <optional>

namespace {

constexpr qsizetype MaxHeaderBytes = 16 * 1024;
constexpr qsizetype MaxBodyBytes = 1024 * 1024;
constexpr int IdleTimeoutMs = 60 * 1000;

QByteArray reasonPhrase(int status) {
    switch (status) {
    case 200:
        return "OK";
    case 201:
        return "Created";
    case 204:
        return "No Content";
    case 400:
        return "Bad Request";
    case 401:
        return "Unauthorized";
    case 403:
        return "Forbidden";
    case 404:
        return "Not Found";
    case 405:
        return "Method Not Allowed";
    case 409:
        return "Conflict";
    case 413:
        return "Payload Too Large";
    case 431:
        return "Request Header Fields Too Large";
    case 500:
        return "Internal Server Error";
    case 501:
        return "Not Implemented";
    case 503:
        return "Service Unavailable";
    default:
        return "Status";
    }
}

enum class ParseResult { Complete, Incomplete, Error };

struct Parsed {
    HttpRequest request;
    bool keepAlive = true;
    qsizetype consumed = 0;
    int errorStatus = 400;
};

// Decodes a chunked body starting at `pos`. Returns the end offset, or nullopt if more data is
// needed.
std::optional<qsizetype> decodeChunked(const QByteArray& buf, qsizetype pos, QByteArray* body,
                                       bool* error) {
    for (;;) {
        const qsizetype lineEnd = buf.indexOf("\r\n", pos);
        if (lineEnd < 0) {
            return std::nullopt;
        }
        bool ok = false;
        const QByteArray sizeField = buf.mid(pos, lineEnd - pos).split(';').first().trimmed();
        const qsizetype size = sizeField.toLongLong(&ok, 16);
        if (!ok || size < 0 || body->size() + size > MaxBodyBytes) {
            *error = true;
            return std::nullopt;
        }
        pos = lineEnd + 2;
        if (size == 0) {
            const qsizetype trailerEnd = buf.indexOf("\r\n\r\n", pos - 2);
            if (trailerEnd < 0) {
                return std::nullopt;
            }
            return trailerEnd + 4;
        }
        if (buf.size() < pos + size + 2) {
            return std::nullopt;
        }
        body->append(buf.constData() + pos, size);
        pos += size + 2;
    }
}

ParseResult parse(const QByteArray& buf, Parsed* out) {
    const qsizetype headerEnd = buf.indexOf("\r\n\r\n");
    if (headerEnd < 0) {
        if (buf.size() > MaxHeaderBytes) {
            out->errorStatus = 431;
            return ParseResult::Error;
        }
        return ParseResult::Incomplete;
    }
    const QList<QByteArray> lines = buf.left(headerEnd).split('\n');
    const QList<QByteArray> requestLine = lines.first().trimmed().split(' ');
    if (requestLine.size() != 3 || !requestLine[2].startsWith("HTTP/1.")) {
        return ParseResult::Error;
    }
    HttpRequest& req = out->request;
    req.method = requestLine[0].toUpper();
    const QByteArray target = requestLine[1];
    const qsizetype q = target.indexOf('?');
    req.path = QUrl::fromPercentEncoding(target.left(q < 0 ? target.size() : q));
    if (q >= 0) {
        req.query = QUrlQuery(QString::fromUtf8(target.mid(q + 1)));
    }
    for (qsizetype i = 1; i < lines.size(); ++i) {
        const QByteArray line = lines[i].trimmed();
        const qsizetype colon = line.indexOf(':');
        if (colon <= 0) {
            continue;
        }
        req.headers.insert(line.left(colon).trimmed().toLower(), line.mid(colon + 1).trimmed());
    }

    const QByteArray connection = req.headers.value("connection").toLower();
    out->keepAlive =
        requestLine[2] == "HTTP/1.1" ? connection != "close" : connection == "keep-alive";

    const qsizetype bodyStart = headerEnd + 4;
    if (req.headers.value("transfer-encoding").toLower().contains("chunked")) {
        bool error = false;
        const auto end = decodeChunked(buf, bodyStart, &req.body, &error);
        if (error) {
            out->errorStatus = 413;
            return ParseResult::Error;
        }
        if (!end) {
            req.body.clear();
            return ParseResult::Incomplete;
        }
        out->consumed = *end;
        return ParseResult::Complete;
    }

    qsizetype length = 0;
    if (req.headers.contains("content-length")) {
        bool ok = false;
        length = req.headers.value("content-length").toLongLong(&ok);
        if (!ok || length < 0) {
            return ParseResult::Error;
        }
        if (length > MaxBodyBytes) {
            out->errorStatus = 413;
            return ParseResult::Error;
        }
    }
    if (buf.size() < bodyStart + length) {
        return ParseResult::Incomplete;
    }
    req.body = buf.mid(bodyStart, length);
    out->consumed = bodyStart + length;
    return ParseResult::Complete;
}

} // namespace

HttpServer::HttpServer(Handler handler, QObject* parent)
    : QObject(parent), handler_(std::move(handler)) {
    connect(&server_, &QTcpServer::newConnection, this, &HttpServer::onNewConnection);
}

bool HttpServer::listen(const QHostAddress& address, quint16 port, QString* error) {
    if (!server_.listen(address, port)) {
        *error = server_.errorString();
        return false;
    }
    return true;
}

void HttpServer::onNewConnection() {
    while (QTcpSocket* socket = server_.nextPendingConnection()) {
        auto* idle = new QTimer(socket);
        idle->setSingleShot(true);
        idle->setInterval(IdleTimeoutMs);
        connect(idle, &QTimer::timeout, socket, &QTcpSocket::disconnectFromHost);
        idle->start();
        socket->setProperty("idleTimer", QVariant::fromValue(static_cast<QObject*>(idle)));
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] { onReadyRead(socket); });
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    }
}

void HttpServer::onReadyRead(QTcpSocket* socket) {
    if (auto* idle = qobject_cast<QTimer*>(socket->property("idleTimer").value<QObject*>())) {
        idle->start();
    }
    socket->setProperty("buffer", socket->property("buffer").toByteArray() + socket->readAll());
    processNext(socket);
}

void HttpServer::processNext(QTcpSocket* socket) {
    if (socket->property("busy").toBool() || socket->property("closing").toBool()) {
        return;
    }
    QByteArray buffer = socket->property("buffer").toByteArray();
    Parsed parsed;
    const ParseResult result = parse(buffer, &parsed);
    if (result == ParseResult::Incomplete) {
        return;
    }
    if (result == ParseResult::Error) {
        HttpResponse response;
        response.status = parsed.errorStatus;
        response.contentType = "text/plain";
        response.body = reasonPhrase(parsed.errorStatus);
        socket->setProperty("buffer", QByteArray());
        send(socket, response, false);
        return;
    }
    buffer.remove(0, parsed.consumed);
    socket->setProperty("buffer", buffer);
    socket->setProperty("busy", true);
    parsed.request.peer = socket->peerAddress();

    QPointer<QTcpSocket> guard(socket);
    const bool keepAlive = parsed.keepAlive;
    handler_(parsed.request, [this, guard, keepAlive](HttpResponse response) {
        if (!guard) {
            return;
        }
        guard->setProperty("busy", false);
        send(guard, response, keepAlive);
        if (keepAlive) {
            QMetaObject::invokeMethod(
                this,
                [this, guard] {
                    if (guard)
                        processNext(guard);
                },
                Qt::QueuedConnection);
        }
    });
}

void HttpServer::send(QTcpSocket* socket, const HttpResponse& response, bool keepAlive) {
    QByteArray head = "HTTP/1.1 " + QByteArray::number(response.status) + ' ' +
                      reasonPhrase(response.status) + "\r\n";
    head += "Content-Type: " + response.contentType + "\r\n";
    head += "Content-Length: " + QByteArray::number(response.body.size()) + "\r\n";
    head += keepAlive ? "Connection: keep-alive\r\n" : "Connection: close\r\n";
    head += "\r\n";
    socket->write(head + response.body);
    if (!keepAlive) {
        socket->setProperty("closing", true);
        socket->disconnectFromHost();
    }
}
