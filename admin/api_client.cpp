// SPDX-FileCopyrightText: Copyright 2026 Resident Evil Revelations Custom Server Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "api_client.h"

#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>

ApiClient::ApiClient(QObject* parent) : QObject(parent) {}

void ApiClient::setServer(const QUrl& base, const QString& key) {
    base_ = base;
    key_ = key;
}

void ApiClient::get(const QString& path, Callback done) {
    send("GET", path, {}, std::move(done));
}

void ApiClient::post(const QString& path, const QJsonDocument& body, Callback done) {
    send("POST", path, body.toJson(QJsonDocument::Compact), std::move(done));
}

void ApiClient::put(const QString& path, const QJsonDocument& body, Callback done) {
    send("PUT", path, body.toJson(QJsonDocument::Compact), std::move(done));
}

void ApiClient::remove(const QString& path, Callback done) {
    send("DELETE", path, {}, std::move(done));
}

void ApiClient::send(const QByteArray& verb, const QString& path, const QByteArray& body,
                     Callback done) {
    const qsizetype q = path.indexOf('?');
    QUrl url = base_;
    url.setPath(url.path().chopped(url.path().endsWith('/') ? 1 : 0) + path.left(q));
    if (q >= 0) {
        url.setQuery(path.mid(q + 1));
    }
    QNetworkRequest request(url);
    request.setRawHeader("X-Admin-Key", key_.toUtf8());
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setTransferTimeout(10000);
    QNetworkReply* reply = net_.sendCustomRequest(request, verb, body);
    connect(reply, &QNetworkReply::finished, this, [reply, done = std::move(done)] {
        reply->deleteLater();
        Reply r;
        r.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        r.body = reply->readAll();
        if (r.status == 0) {
            r.error = reply->errorString();
        } else if (r.status >= 400) {
            const QString message =
                QJsonDocument::fromJson(r.body).object().value("error").toString();
            r.error = message.isEmpty() ? QStringLiteral("HTTP %1").arg(r.status) : message;
        }
        done(r);
    });
}
