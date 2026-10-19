// SPDX-FileCopyrightText: Copyright 2026 Resident Evil Revelations Custom Server Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QHash>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QObject>
#include <QString>
#include <QVector>

#include <functional>

// Turns NP authorization codes into accounts the way a PSN backend does: the code is exchanged for
// an access token (POST /2.0/oauth/token), which is then validated (GET /2.0/oauth/token/<token>).
// The game asks for a new code for every request, each is redeemed once, and the answer is cached
// so a retried request with the same code does not need shadNet again.
class ShadNetVerifier : public QObject {
    Q_OBJECT
public:
    enum class Status { Verified, Rejected, Unavailable };

    struct Result {
        Status status = Status::Unavailable;
        QString accountId;
        QString onlineId;
        QString clientId;
        QString message;
    };

    using Callback = std::function<void(const Result&)>;

    // The game's NP client ID. Codes are redeemed as this client, and only codes issued to it
    // are accepted.
    static constexpr const char* GameClientId = "d3ac8b19-8ba3-44da-95c5-e8ce70f816e9";

    explicit ShadNetVerifier(QObject* parent = nullptr);

    void setEndpoint(const QString& baseUrl);
    void verify(const QString& code, Callback callback);

private:
    struct CacheEntry {
        Result result;
        qint64 cachedAt = 0;
    };

    QNetworkRequest authRequest(const QString& path) const;
    void validate(const QString& code, const QString& accessToken);
    static Result failure(int status, const QJsonObject& body, const QString& networkError);
    void finish(const QString& code, const Result& result);
    void prune(qint64 now);

    QNetworkAccessManager network_;
    QString baseUrl_;
    QHash<QString, CacheEntry> cache_;
    QHash<QString, QVector<Callback>> pending_;
};
