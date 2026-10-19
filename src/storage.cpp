// SPDX-FileCopyrightText: Copyright 2026 Resident Evil Revelations Custom Server Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "storage.h"

#include <QDateTime>
#include <QRandomGenerator>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace {

qint64 now() {
    return QDateTime::currentSecsSinceEpoch();
}

QString newPlayerCode() {
    static const char alphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    QString code;
    for (int i = 0; i < 16; ++i) {
        code +=
            QLatin1Char(alphabet[QRandomGenerator::system()->bounded(int(sizeof(alphabet) - 1))]);
    }
    return code;
}

} // namespace

Storage::~Storage() {
    if (db_.isValid()) {
        db_.close();
        db_ = QSqlDatabase();
        QSqlDatabase::removeDatabase(connectionName_);
    }
}

bool Storage::open(const QString& path, QString* error) {
    connectionName_ = QStringLiteral("revelations");
    db_ = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName_);
    db_.setDatabaseName(path);
    if (!db_.open()) {
        *error = db_.lastError().text();
        return false;
    }
    bool ok =
        exec(QStringLiteral("PRAGMA journal_mode=WAL")) &&
        exec(QStringLiteral("PRAGMA foreign_keys=ON")) &&
        exec(QStringLiteral("CREATE TABLE IF NOT EXISTS players ("
                            " id INTEGER PRIMARY KEY AUTOINCREMENT,"
                            " identity TEXT NOT NULL UNIQUE,"
                            " code TEXT NOT NULL UNIQUE,"
                            " created INTEGER NOT NULL,"
                            " last_seen INTEGER NOT NULL)")) &&
        exec(QStringLiteral("CREATE TABLE IF NOT EXISTS uploads ("
                            " player_id INTEGER PRIMARY KEY REFERENCES players(id),"
                            " updated INTEGER NOT NULL,"
                            " data BLOB NOT NULL)")) &&
        exec(QStringLiteral("CREATE TABLE IF NOT EXISTS claims ("
                            " player_id INTEGER NOT NULL REFERENCES players(id),"
                            " serial INTEGER NOT NULL,"
                            " claimed INTEGER NOT NULL,"
                            " PRIMARY KEY (player_id, serial))")) &&
        exec(QStringLiteral("CREATE TABLE IF NOT EXISTS admin_rewards ("
                            " serial INTEGER PRIMARY KEY,"
                            " data TEXT NOT NULL,"
                            " created INTEGER NOT NULL)")) &&
        exec(QStringLiteral("CREATE TABLE IF NOT EXISTS upload_history ("
                            " id INTEGER PRIMARY KEY AUTOINCREMENT,"
                            " player_id INTEGER NOT NULL REFERENCES players(id),"
                            " received INTEGER NOT NULL,"
                            " data BLOB NOT NULL)")) &&
        exec(QStringLiteral("CREATE INDEX IF NOT EXISTS upload_history_player ON upload_history "
                            "(player_id, id)")) &&
        // Databases from before the history start it with each player's last upload.
        exec(QStringLiteral("INSERT INTO upload_history (player_id, received, data)"
                            " SELECT player_id, updated, data FROM uploads"
                            " WHERE player_id NOT IN (SELECT player_id FROM upload_history)")) &&
        exec(QStringLiteral("CREATE TABLE IF NOT EXISTS admin_settings ("
                            " key TEXT PRIMARY KEY,"
                            " value TEXT NOT NULL)"));
    if (ok && !hasColumn(QStringLiteral("players"), QStringLiteral("online_id"))) {
        ok = exec(QStringLiteral("ALTER TABLE players ADD COLUMN online_id TEXT"));
    }
    if (!ok) {
        *error = lastError();
    }
    return ok;
}

std::optional<Player> Storage::player(const QString& identity, const QString& onlineId) {
    QSqlQuery find(db_);
    find.prepare(QStringLiteral("SELECT id, code FROM players WHERE identity = ?"));
    find.addBindValue(identity);
    if (!find.exec()) {
        return std::nullopt;
    }
    if (find.next()) {
        Player p{find.value(0).toLongLong(), find.value(1).toString()};
        QSqlQuery touch(db_);
        touch.prepare(QStringLiteral("UPDATE players SET last_seen = ?, online_id = "
                                     "COALESCE(NULLIF(?, ''), online_id) WHERE id = ?"));
        touch.addBindValue(now());
        touch.addBindValue(onlineId);
        touch.addBindValue(p.id);
        touch.exec();
        return p;
    }

    for (int attempt = 0; attempt < 8; ++attempt) {
        const QString code = newPlayerCode();
        QSqlQuery insert(db_);
        insert.prepare(QStringLiteral("INSERT INTO players (identity, code, online_id, created, "
                                      "last_seen) VALUES (?, ?, NULLIF(?, ''), ?, ?)"));
        insert.addBindValue(identity);
        insert.addBindValue(code);
        insert.addBindValue(onlineId);
        insert.addBindValue(now());
        insert.addBindValue(now());
        if (insert.exec()) {
            return Player{insert.lastInsertId().toLongLong(), code};
        }
    }
    return std::nullopt;
}

bool Storage::storeUpload(qint64 playerId, const QByteArray& data) {
    if (!db_.transaction()) {
        return false;
    }
    // The game uploads after every stage, an unchanged record adds nothing to the history.
    QSqlQuery last(db_);
    last.prepare(QStringLiteral(
        "SELECT data FROM upload_history WHERE player_id = ? ORDER BY id DESC LIMIT 1"));
    last.addBindValue(playerId);
    const bool same = last.exec() && last.next() && last.value(0).toByteArray() == data;
    if (!same) {
        QSqlQuery h(db_);
        h.prepare(QStringLiteral(
            "INSERT INTO upload_history (player_id, received, data) VALUES (?, ?, ?)"));
        h.addBindValue(playerId);
        h.addBindValue(now());
        h.addBindValue(data);
        if (!h.exec()) {
            db_.rollback();
            return false;
        }
    }
    QSqlQuery q(db_);
    q.prepare(QStringLiteral(
        "INSERT INTO uploads (player_id, updated, data) VALUES (?, ?, ?)"
        " ON CONFLICT(player_id) DO UPDATE SET updated = excluded.updated, data = excluded.data"));
    q.addBindValue(playerId);
    q.addBindValue(now());
    q.addBindValue(data);
    if (!q.exec()) {
        db_.rollback();
        return false;
    }
    return db_.commit();
}

QSet<qint64> Storage::claimedSerials(qint64 playerId) {
    QSet<qint64> serials;
    QSqlQuery q(db_);
    q.prepare(QStringLiteral("SELECT serial FROM claims WHERE player_id = ?"));
    q.addBindValue(playerId);
    if (q.exec()) {
        while (q.next()) {
            serials.insert(q.value(0).toLongLong());
        }
    }
    return serials;
}

bool Storage::claim(qint64 playerId, const QSet<qint64>& serials) {
    if (!db_.transaction()) {
        return false;
    }
    for (const qint64 serial : serials) {
        QSqlQuery q(db_);
        q.prepare(QStringLiteral(
            "INSERT OR IGNORE INTO claims (player_id, serial, claimed) VALUES (?, ?, ?)"));
        q.addBindValue(playerId);
        q.addBindValue(serial);
        q.addBindValue(now());
        if (!q.exec()) {
            db_.rollback();
            return false;
        }
    }
    return db_.commit();
}

std::optional<QVector<PlayerInfo>> Storage::players() {
    QSqlQuery q(db_);
    if (!q.exec(QStringLiteral(
            "SELECT p.id, p.identity, p.code, COALESCE(p.online_id, ''), p.created, p.last_seen,"
            " COALESCE(u.updated, 0), COALESCE(length(u.data), 0)"
            " FROM players p LEFT JOIN uploads u ON u.player_id = p.id ORDER BY p.last_seen "
            "DESC"))) {
        return std::nullopt;
    }
    QVector<PlayerInfo> list;
    QHash<qint64, qsizetype> index;
    while (q.next()) {
        PlayerInfo p;
        p.id = q.value(0).toLongLong();
        p.identity = q.value(1).toString();
        p.code = q.value(2).toString();
        p.onlineId = q.value(3).toString();
        p.created = q.value(4).toLongLong();
        p.lastSeen = q.value(5).toLongLong();
        p.uploadUpdated = q.value(6).toLongLong();
        p.uploadSize = q.value(7).toLongLong();
        index.insert(p.id, list.size());
        list.append(p);
    }
    QSqlQuery c(db_);
    if (!c.exec(QStringLiteral("SELECT player_id, serial, claimed FROM claims ORDER BY claimed"))) {
        return std::nullopt;
    }
    while (c.next()) {
        const auto it = index.constFind(c.value(0).toLongLong());
        if (it != index.constEnd()) {
            list[*it].claims.append({c.value(1).toLongLong(), c.value(2).toLongLong()});
        }
    }
    return list;
}

bool Storage::playerExists(qint64 playerId) {
    QSqlQuery q(db_);
    q.prepare(QStringLiteral("SELECT 1 FROM players WHERE id = ?"));
    q.addBindValue(playerId);
    return q.exec() && q.next();
}

std::optional<QPair<QByteArray, qint64>> Storage::upload(qint64 playerId) {
    QSqlQuery q(db_);
    q.prepare(QStringLiteral("SELECT data, updated FROM uploads WHERE player_id = ?"));
    q.addBindValue(playerId);
    if (!q.exec() || !q.next()) {
        return std::nullopt;
    }
    return QPair<QByteArray, qint64>{q.value(0).toByteArray(), q.value(1).toLongLong()};
}

std::optional<QVector<HistoryUpload>> Storage::history(qint64 playerId, bool withData) {
    QSqlQuery q(db_);
    q.prepare(
        withData
            ? QStringLiteral(
                  "SELECT id, received, data FROM upload_history WHERE player_id = ? ORDER BY id")
            : QStringLiteral(
                  "SELECT id, received FROM upload_history WHERE player_id = ? ORDER BY id"));
    q.addBindValue(playerId);
    if (!q.exec()) {
        return std::nullopt;
    }
    QVector<HistoryUpload> list;
    while (q.next()) {
        list.append({q.value(0).toLongLong(), q.value(1).toLongLong(),
                     withData ? q.value(2).toByteArray() : QByteArray()});
    }
    return list;
}

std::optional<HistoryUpload> Storage::historyUpload(qint64 playerId, qint64 uploadId) {
    QSqlQuery q(db_);
    q.prepare(QStringLiteral(
        "SELECT id, received, data FROM upload_history WHERE player_id = ? AND id = ?"));
    q.addBindValue(playerId);
    q.addBindValue(uploadId);
    if (!q.exec() || !q.next()) {
        return std::nullopt;
    }
    return HistoryUpload{q.value(0).toLongLong(), q.value(1).toLongLong(),
                         q.value(2).toByteArray()};
}

std::optional<HistoryUpload> Storage::previousUpload(qint64 playerId, qint64 uploadId) {
    QSqlQuery q(db_);
    q.prepare(QStringLiteral("SELECT id, received, data FROM upload_history WHERE player_id = ? "
                             "AND id < ? ORDER BY id DESC LIMIT 1"));
    q.addBindValue(playerId);
    q.addBindValue(uploadId);
    if (!q.exec() || !q.next()) {
        return std::nullopt;
    }
    return HistoryUpload{q.value(0).toLongLong(), q.value(1).toLongLong(),
                         q.value(2).toByteArray()};
}

int Storage::resetClaims(qint64 playerId, std::optional<qint64> serial) {
    QSqlQuery q(db_);
    if (serial) {
        q.prepare(QStringLiteral("DELETE FROM claims WHERE player_id = ? AND serial = ?"));
        q.addBindValue(playerId);
        q.addBindValue(*serial);
    } else {
        q.prepare(QStringLiteral("DELETE FROM claims WHERE player_id = ?"));
        q.addBindValue(playerId);
    }
    if (!q.exec()) {
        return -1;
    }
    return q.numRowsAffected();
}

QHash<qint64, int> Storage::claimCounts() {
    QHash<qint64, int> counts;
    QSqlQuery q(db_);
    if (q.exec(QStringLiteral("SELECT serial, COUNT(*) FROM claims GROUP BY serial"))) {
        while (q.next()) {
            counts.insert(q.value(0).toLongLong(), q.value(1).toInt());
        }
    }
    return counts;
}

std::optional<QVector<StoredReward>> Storage::storedRewards() {
    QSqlQuery q(db_);
    if (!q.exec(
            QStringLiteral("SELECT serial, data, created FROM admin_rewards ORDER BY serial"))) {
        return std::nullopt;
    }
    QVector<StoredReward> list;
    while (q.next()) {
        list.append(
            {q.value(0).toLongLong(), q.value(1).toString().toUtf8(), q.value(2).toLongLong()});
    }
    return list;
}

bool Storage::addStoredReward(qint64 serial, const QByteArray& json) {
    QSqlQuery q(db_);
    q.prepare(QStringLiteral("INSERT INTO admin_rewards (serial, data, created) VALUES (?, ?, ?)"));
    q.addBindValue(serial);
    q.addBindValue(QString::fromUtf8(json));
    q.addBindValue(now());
    return q.exec();
}

bool Storage::removeStoredReward(qint64 serial) {
    QSqlQuery q(db_);
    q.prepare(QStringLiteral("DELETE FROM admin_rewards WHERE serial = ?"));
    q.addBindValue(serial);
    return q.exec() && q.numRowsAffected() > 0;
}

std::optional<QByteArray> Storage::setting(const QString& key) {
    QSqlQuery q(db_);
    q.prepare(QStringLiteral("SELECT value FROM admin_settings WHERE key = ?"));
    q.addBindValue(key);
    if (!q.exec() || !q.next()) {
        return std::nullopt;
    }
    return q.value(0).toString().toUtf8();
}

bool Storage::setSetting(const QString& key, const QByteArray& value) {
    QSqlQuery q(db_);
    q.prepare(QStringLiteral("INSERT INTO admin_settings (key, value) VALUES (?, ?)"
                             " ON CONFLICT(key) DO UPDATE SET value = excluded.value"));
    q.addBindValue(key);
    q.addBindValue(QString::fromUtf8(value));
    return q.exec();
}

bool Storage::removeSetting(const QString& key) {
    QSqlQuery q(db_);
    q.prepare(QStringLiteral("DELETE FROM admin_settings WHERE key = ?"));
    q.addBindValue(key);
    return q.exec();
}

bool Storage::hasColumn(const QString& table, const QString& column) {
    QSqlQuery q(db_);
    if (!q.exec(QStringLiteral("PRAGMA table_info(%1)").arg(table))) {
        return false;
    }
    while (q.next()) {
        if (q.value(1).toString() == column) {
            return true;
        }
    }
    return false;
}

bool Storage::exec(const QString& sql) {
    QSqlQuery q(db_);
    return q.exec(sql);
}

QString Storage::lastError() const {
    return db_.lastError().text();
}
