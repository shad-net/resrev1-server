// SPDX-FileCopyrightText: Copyright 2026 Resident Evil Revelations Custom Server Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "shadnet_verifier.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

namespace {

constexpr qint64 CacheLifetimeSecs = 12 * 60 * 60;
constexpr qsizetype MaxCachedCodes = 10000;
constexpr int RequestTimeoutMs = 10000;

} // namespace

ShadNetVerifier::ShadNetVerifier(QObject* parent) : QObject(parent) {}

void ShadNetVerifier::setEndpoint(const QString& baseUrl) {
    if (baseUrl != baseUrl_) {
        cache_.clear();
    }
    baseUrl_ = baseUrl;
}

QNetworkRequest ShadNetVerifier::authRequest(const QString& path) const {
    QNetworkRequest request(QUrl(baseUrl_ + path));
    // shadNet does not check client secrets, so the secret part is empty.
    const QByteArray credentials = QByteArray(GameClientId) + ':';
    request.setRawHeader("Authorization", "Basic " + credentials.toBase64());
    request.setTransferTimeout(RequestTimeoutMs);
    return request;
}

ShadNetVerifier::Result ShadNetVerifier::failure(int status, const QJsonObject& body,
                                                 const QString& networkError) {
    Result result;
    if (status >= 400 && status < 500) {
        result.status = Status::Rejected;
        result.message = body.value(QStringLiteral("error")).toString();
        const QString description = body.value(QStringLiteral("error_description")).toString();
        if (!description.isEmpty()) {
            result.message += QStringLiteral(": ") + description;
        }
    } else {
        result.status = Status::Unavailable;
        result.message = status ? QStringLiteral("HTTP %1").arg(status) : networkError;
    }
    return result;
}

void ShadNetVerifier::verify(const QString& code, Callback callback) {
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    prune(now);
    if (const auto it = cache_.constFind(code); it != cache_.constEnd()) {
        callback(it->result);
        return;
    }
    auto& waiting = pending_[code];
    waiting.append(std::move(callback));
    if (waiting.size() > 1) {
        return;
    }

    const QByteArray form =
        "grant_type=authorization_code&code=" + QUrl::toPercentEncoding(code) +
        "&redirect_uri=" + QUrl::toPercentEncoding(QStringLiteral("orbis://games"));
    QNetworkRequest request = authRequest(QStringLiteral("/2.0/oauth/token"));
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QByteArrayLiteral("application/x-www-form-urlencoded"));
    QNetworkReply* reply = network_.post(request, form);
    connect(reply, &QNetworkReply::finished, this, [this, reply, code] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QJsonObject body = QJsonDocument::fromJson(reply->readAll()).object();
        const QString accessToken = body.value(QStringLiteral("access_token")).toString();
        if (status == 200 && !accessToken.isEmpty()) {
            validate(code, accessToken);
        } else {
            finish(code, failure(status, body, reply->errorString()));
        }
    });
}

void ShadNetVerifier::validate(const QString& code, const QString& accessToken) {
    const QString path = QStringLiteral("/2.0/oauth/token/") +
                         QString::fromLatin1(QUrl::toPercentEncoding(accessToken));
    QNetworkReply* reply = network_.get(authRequest(path));
    connect(reply, &QNetworkReply::finished, this, [this, reply, code] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QJsonObject body = QJsonDocument::fromJson(reply->readAll()).object();
        if (status != 200 || body.value(QStringLiteral("user_id")).toString().isEmpty()) {
            finish(code, failure(status, body, reply->errorString()));
            return;
        }
        Result result;
        result.status = Status::Verified;
        result.accountId = body.value(QStringLiteral("user_id")).toString();
        result.onlineId = body.value(QStringLiteral("online_id")).toString();
        result.clientId = body.value(QStringLiteral("client_id")).toString();
        finish(code, result);
    });
}

void ShadNetVerifier::finish(const QString& code, const Result& result) {
    if (result.status == Status::Verified) {
        cache_.insert(code, CacheEntry{result, QDateTime::currentSecsSinceEpoch()});
    }
    const QVector<Callback> waiting = pending_.take(code);
    for (const Callback& callback : waiting) {
        callback(result);
    }
}

void ShadNetVerifier::prune(qint64 now) {
    for (auto it = cache_.begin(); it != cache_.end();) {
        it = now - it->cachedAt > CacheLifetimeSecs ? cache_.erase(it) : std::next(it);
    }
    while (cache_.size() >= MaxCachedCodes) {
        auto oldest = cache_.begin();
        for (auto it = cache_.begin(); it != cache_.end(); ++it) {
            if (it->cachedAt < oldest->cachedAt) {
                oldest = it;
            }
        }
        cache_.erase(oldest);
    }
}
