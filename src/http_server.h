// SPDX-FileCopyrightText: Copyright 2026 Resident Evil Revelations Custom Server Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QByteArray>
#include <QHash>
#include <QHostAddress>
#include <QObject>
#include <QString>
#include <QTcpServer>
#include <QUrlQuery>

#include <functional>

class QTcpSocket;

struct HttpRequest {
    QByteArray method;
    QString path;
    QUrlQuery query;
    QHash<QByteArray, QByteArray> headers;
    QByteArray body;
    QHostAddress peer;
};

struct HttpResponse {
    int status = 200;
    QByteArray contentType = QByteArrayLiteral("application/octet-stream");
    QByteArray body;
};

// Small HTTP/1.1 server: keep-alive, Content-Length and chunked request bodies.
class HttpServer : public QObject {
    Q_OBJECT
public:
    using Responder = std::function<void(HttpResponse)>;
    using Handler = std::function<void(const HttpRequest&, Responder)>;

    explicit HttpServer(Handler handler, QObject* parent = nullptr);

    bool listen(const QHostAddress& address, quint16 port, QString* error);

private:
    void onNewConnection();
    void onReadyRead(QTcpSocket* socket);
    void processNext(QTcpSocket* socket);
    void send(QTcpSocket* socket, const HttpResponse& response, bool keepAlive);

    Handler handler_;
    QTcpServer server_;
};
