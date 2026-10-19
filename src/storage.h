// SPDX-FileCopyrightText: Copyright 2026 Resident Evil Revelations Custom Server Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QByteArray>
#include <QHash>
#include <QSet>
#include <QSqlDatabase>
#include <QString>
#include <QVector>

#include <optional>

struct Player {
    qint64 id = 0;
    QString code;
};

struct PlayerInfo {
    qint64 id = 0;
    QString identity;
    QString code;
    QString onlineId;
    qint64 created = 0;
    qint64 lastSeen = 0;
    qint64 uploadUpdated = 0; // 0 when the player never uploaded
    qint64 uploadSize = 0;
    QVector<QPair<qint64, qint64>> claims; // serial, claimed time
};

struct HistoryUpload {
    qint64 id = 0;
    qint64 received = 0;
    QByteArray data; // empty in listings
};

struct StoredReward {
    qint64 serial = 0;
    QByteArray json;
    qint64 created = 0;
};

class Storage {
public:
    ~Storage();

    bool open(const QString& path, QString* error);
    std::optional<Player> player(const QString& identity, const QString& onlineId = {});
    bool storeUpload(qint64 playerId, const QByteArray& data);
    QSet<qint64> claimedSerials(qint64 playerId);
    bool claim(qint64 playerId, const QSet<qint64>& serials);

    // Admin queries.
    std::optional<QVector<PlayerInfo>> players();
    bool playerExists(qint64 playerId);
    std::optional<QPair<QByteArray, qint64>> upload(qint64 playerId);
    std::optional<QVector<HistoryUpload>> history(qint64 playerId, bool withData);
    std::optional<HistoryUpload> historyUpload(qint64 playerId, qint64 uploadId);
    std::optional<HistoryUpload> previousUpload(qint64 playerId, qint64 uploadId);
    int resetClaims(qint64 playerId, std::optional<qint64> serial);
    QHash<qint64, int> claimCounts();
    std::optional<QByteArray> setting(const QString& key);
    bool setSetting(const QString& key, const QByteArray& value);
    bool removeSetting(const QString& key);
    std::optional<QVector<StoredReward>> storedRewards();
    bool addStoredReward(qint64 serial, const QByteArray& json);
    bool removeStoredReward(qint64 serial);

private:
    bool exec(const QString& sql);
    bool hasColumn(const QString& table, const QString& column);
    QString lastError() const;

    QString connectionName_;
    QSqlDatabase db_;
};
