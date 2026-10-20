// SPDX-FileCopyrightText: Copyright 2026 Resident Evil Revelations Custom Server Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "admin_service.h"

#include "config.h"
#include "leaderboard.h"
#include "revelations_service.h"
#include "storage.h"
#include "upload_decoder.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QSet>

#include <algorithm>

namespace {

Q_LOGGING_CATEGORY(lcAdmin, "admin")

const QString EventSetting = QStringLiteral("event");

QJsonObject eventObject(const QByteArray& json) {
    return QJsonDocument::fromJson(json).object();
}

HttpResponse json(int status, const QJsonValue& value) {
    HttpResponse r;
    r.status = status;
    r.contentType = "application/json";
    r.body = value.isArray() ? QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact)
                             : QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact);
    return r;
}

HttpResponse error(int status, const QString& message) {
    return json(status, QJsonObject{{"error", message}});
}

// Compares digests so the time taken does not depend on how much of the key matches.
bool sameKey(const QByteArray& given, const QString& expected) {
    const QByteArray a = QCryptographicHash::hash(given, QCryptographicHash::Sha256);
    const QByteArray b = QCryptographicHash::hash(expected.toUtf8(), QCryptographicHash::Sha256);
    unsigned char diff = 0;
    for (int i = 0; i < a.size(); ++i) {
        diff |= static_cast<unsigned char>(a[i] ^ b[i]);
    }
    return diff == 0;
}

std::optional<qint64> parseId(const QString& text) {
    bool ok = false;
    const qint64 v = text.toLongLong(&ok);
    return ok && v > 0 ? std::optional<qint64>(v) : std::nullopt;
}

} // namespace

AdminService::AdminService(Storage* storage, RevelationsService* service)
    : storage_(storage), service_(service) {
    uptime_.start();
}

bool AdminService::loadStored(QString* error) {
    if (const auto event = storage_->setting(EventSetting)) {
        QByteArray normalized;
        QString why;
        if (parseEventJson(QJsonDocument::fromJson(*event).object(), &normalized, &why)) {
            service_->setEventOverride(normalized);
            qCInfo(lcAdmin) << "using the event set through the admin API";
        } else {
            qCWarning(lcAdmin).noquote() << "ignoring stored event:" << why;
        }
    }
    const auto stored = storage_->storedRewards();
    if (!stored) {
        *error = QStringLiteral("cannot read admin_rewards");
        return false;
    }
    QVector<Reward> rewards;
    for (const StoredReward& s : *stored) {
        Reward r;
        QString why;
        const QJsonObject obj = QJsonDocument::fromJson(s.json).object();
        if (!parseReward(obj, QStringLiteral("admin reward %1").arg(s.serial), &r, &why)) {
            qCWarning(lcAdmin).noquote() << "skipping stored reward:" << why;
            continue;
        }
        rewards.append(r);
    }
    service_->setStoredRewards(rewards);
    return true;
}

void AdminService::handle(const HttpRequest& request, HttpServer::Responder respond) {
    const QString& key = service_->config().adminKey;
    if (key.isEmpty() || !sameKey(request.headers.value("x-admin-key"), key)) {
        qCWarning(lcAdmin).noquote()
            << request.method << request.path << "from" << request.peer.toString() << "-> 401";
        respond(error(401, QStringLiteral("missing or wrong X-Admin-Key")));
        return;
    }
    const HttpResponse response = route(request);
    qCInfo(lcAdmin).noquote() << request.method << request.path << "->" << response.status;
    respond(response);
}

HttpResponse AdminService::route(const HttpRequest& request) {
    const QStringList parts = request.path.split('/', Qt::SkipEmptyParts);
    const QByteArray& m = request.method;
    if (parts.size() < 2 || parts[0] != QLatin1String("api")) {
        return error(404, QStringLiteral("not found"));
    }
    const QString& what = parts[1];
    if (what == QLatin1String("status") && parts.size() == 2 && m == "GET") {
        return status();
    }
    if (what == QLatin1String("players")) {
        if (parts.size() == 2 && m == "GET") {
            return players();
        }
        if (parts.size() == 4) {
            const auto id = parseId(parts[2]);
            if (!id || !storage_->playerExists(*id)) {
                return error(404, QStringLiteral("no such player"));
            }
            if (parts[3] == QLatin1String("upload") && m == "GET") {
                return upload(*id);
            }
            if (parts[3] == QLatin1String("stats") && m == "GET") {
                return stats(*id, request.query.queryItemValue(QStringLiteral("upload")));
            }
            if (parts[3] == QLatin1String("history") && m == "GET") {
                return history(*id);
            }
            if (parts[3] == QLatin1String("reset-claims") && m == "POST") {
                return resetClaims(*id, request.body);
            }
        }
    }
    if (what == QLatin1String("event") && parts.size() == 2) {
        if (m == "GET") {
            return event();
        }
        if (m == "PUT" || m == "POST") {
            return setEvent(request.body);
        }
        if (m == "DELETE") {
            return clearEvent();
        }
    }
    if (what == QLatin1String("leaderboards") && parts.size() == 2 && m == "GET") {
        return leaderboards(request.query.queryItemValue(QStringLiteral("size")));
    }
    if (what == QLatin1String("rewards")) {
        if (parts.size() == 2 && m == "GET") {
            return rewards();
        }
        if (parts.size() == 2 && m == "POST") {
            return addReward(request.body);
        }
        if (parts.size() == 3 && m == "DELETE") {
            const auto serial = parseId(parts[2]);
            if (!serial) {
                return error(400, QStringLiteral("bad serial"));
            }
            return removeReward(*serial);
        }
    }
    return error(404, QStringLiteral("not found"));
}

HttpResponse AdminService::status() {
    const ServerConfig& c = service_->config();
    const auto list = storage_->players();
    return json(200, QJsonObject{
                         {"version", QStringLiteral("1.0")},
                         {"uptime", uptime_.elapsed() / 1000},
                         {"listen", QStringLiteral("%1:%2").arg(c.address.toString()).arg(c.port)},
                         {"base_path", c.basePath},
                         {"shadnet_url", c.shadnetUrl},
                         {"players", list ? qint64(list->size()) : -1},
                         {"config_rewards", qint64(c.rewards.size())},
                         {"admin_rewards", qint64(service_->storedRewards().size())},
                         {"event_source", service_->eventOverride() ? QStringLiteral("admin")
                                                                    : QStringLiteral("config")},
                     });
}

HttpResponse AdminService::players() {
    const auto list = storage_->players();
    if (!list) {
        return error(500, QStringLiteral("storage error"));
    }
    QJsonArray out;
    for (const PlayerInfo& p : *list) {
        QJsonArray claims;
        for (const auto& [serial, when] : p.claims) {
            claims.append(QJsonObject{{"serial", serial}, {"claimed", when}});
        }
        out.append(QJsonObject{
            {"id", p.id},
            {"identity", p.identity},
            {"code", p.code},
            {"online_id", p.onlineId},
            {"created", p.created},
            {"last_seen", p.lastSeen},
            {"upload_updated", p.uploadUpdated},
            {"upload_size", p.uploadSize},
            {"claims", claims},
        });
    }
    return json(200, out);
}

HttpResponse AdminService::upload(qint64 playerId) {
    const auto blob = storage_->upload(playerId);
    if (!blob) {
        return error(404, QStringLiteral("this player has not uploaded anything"));
    }
    HttpResponse r;
    r.body = blob->first;
    return r;
}

HttpResponse AdminService::stats(qint64 playerId, const QString& uploadId) {
    std::optional<HistoryUpload> current;
    if (uploadId.isEmpty()) {
        const auto list = storage_->history(playerId, false);
        if (list && !list->isEmpty()) {
            current = storage_->historyUpload(playerId, list->last().id);
        }
    } else {
        const auto id = parseId(uploadId);
        if (!id) {
            return error(400, QStringLiteral("bad upload id"));
        }
        current = storage_->historyUpload(playerId, *id);
    }
    if (!current) {
        return error(404, uploadId.isEmpty()
                              ? QStringLiteral("this player has not uploaded anything")
                              : QStringLiteral("no such upload"));
    }
    QJsonObject out = decodeUpload(current->data);
    out.insert("upload_id", current->id);
    out.insert("uploaded", current->received);
    if (const auto previous = storage_->previousUpload(playerId, current->id)) {
        out.insert("previous_id", previous->id);
        out.insert("previous_uploaded", previous->received);
        out.insert("changes", compareUploads(previous->data, current->data));
    }
    // Bests up to and including this upload.
    if (const auto all = storage_->history(playerId, true)) {
        QVector<QPair<QByteArray, qint64>> uploads;
        for (const HistoryUpload& h : *all) {
            if (h.id <= current->id) {
                uploads.append({h.data, h.received});
            }
        }
        out.insert("bests", personalBests(uploads));
    }
    return json(200, out);
}

HttpResponse AdminService::history(qint64 playerId) {
    const auto list = storage_->history(playerId, false);
    if (!list) {
        return error(500, QStringLiteral("storage error"));
    }
    QJsonArray out;
    for (const HistoryUpload& h : *list) {
        out.append(QJsonObject{{"id", h.id}, {"received", h.received}});
    }
    return json(200, out);
}

HttpResponse AdminService::resetClaims(qint64 playerId, const QByteArray& body) {
    std::optional<qint64> serial;
    if (!body.trimmed().isEmpty()) {
        const QJsonObject obj = QJsonDocument::fromJson(body).object();
        if (obj.contains("serial")) {
            const qint64 v = obj.value("serial").toInteger(0);
            if (v > 0) {
                serial = v;
            } else {
                return error(400, QStringLiteral("serial must be a positive integer"));
            }
        }
    }
    const int removed = storage_->resetClaims(playerId, serial);
    if (removed < 0) {
        return error(500, QStringLiteral("storage error"));
    }
    qCInfo(lcAdmin) << "reset" << removed << "claim(s) of player" << playerId;
    return json(200, QJsonObject{{"removed", removed}});
}

HttpResponse AdminService::rewards() {
    const QHash<qint64, int> counts = storage_->claimCounts();
    QSet<qint64> configSerials;
    QJsonArray out;
    for (const Reward& r : service_->config().rewards) {
        QJsonObject obj = rewardToJson(r);
        obj.insert("source", QStringLiteral("config"));
        obj.insert("claims", counts.value(r.serial));
        out.append(obj);
        configSerials.insert(r.serial);
    }
    for (const Reward& r : service_->storedRewards()) {
        QJsonObject obj = rewardToJson(r);
        obj.insert("source", QStringLiteral("admin"));
        obj.insert("claims", counts.value(r.serial));
        if (configSerials.contains(r.serial)) {
            obj.insert("hidden", true);
        }
        out.append(obj);
    }
    return json(200, out);
}

HttpResponse AdminService::addReward(const QByteArray& body) {
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
    if (!doc.isObject()) {
        return error(400, QStringLiteral("body must be a reward object"));
    }
    Reward reward;
    QString why;
    if (!parseReward(doc.object(), QStringLiteral("reward"), &reward, &why)) {
        return error(400, why);
    }
    for (const Reward& r : service_->config().rewards) {
        if (r.serial == reward.serial) {
            return error(409,
                         QStringLiteral("serial %1 is used by a config reward").arg(reward.serial));
        }
    }
    for (const Reward& r : service_->storedRewards()) {
        if (r.serial == reward.serial) {
            return error(409, QStringLiteral("serial %1 is already used").arg(reward.serial));
        }
    }
    const QJsonObject normalized = rewardToJson(reward);
    if (!storage_->addStoredReward(reward.serial,
                                   QJsonDocument(normalized).toJson(QJsonDocument::Compact))) {
        return error(500, QStringLiteral("storage error"));
    }
    QVector<Reward> stored = service_->storedRewards();
    stored.append(reward);
    service_->setStoredRewards(stored);
    qCInfo(lcAdmin).noquote() << "added reward"
                              << QJsonDocument(normalized).toJson(QJsonDocument::Compact);
    return json(201, normalized);
}

HttpResponse AdminService::removeReward(qint64 serial) {
    QVector<Reward> stored = service_->storedRewards();
    const auto it = std::find_if(stored.begin(), stored.end(),
                                 [serial](const Reward& r) { return r.serial == serial; });
    if (it == stored.end()) {
        for (const Reward& r : service_->config().rewards) {
            if (r.serial == serial) {
                return error(409,
                             QStringLiteral("serial %1 comes from config.json; remove it there")
                                 .arg(serial));
            }
        }
        return error(404, QStringLiteral("no such reward"));
    }
    if (!storage_->removeStoredReward(serial)) {
        return error(500, QStringLiteral("storage error"));
    }
    stored.erase(it);
    service_->setStoredRewards(stored);
    qCInfo(lcAdmin) << "removed reward" << serial;
    return json(200, QJsonObject{{"removed", serial}});
}

HttpResponse AdminService::event() {
    return json(200, QJsonObject{
                         {"source", service_->eventOverride() ? QStringLiteral("admin")
                                                              : QStringLiteral("config")},
                         {"event", eventObject(service_->eventJson())},
                         {"config_event", eventObject(service_->config().eventJson)},
                     });
}

HttpResponse AdminService::setEvent(const QByteArray& body) {
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (!doc.isObject()) {
        return error(400, QStringLiteral("body must be an event object"));
    }
    QByteArray normalized;
    QString why;
    if (!parseEventJson(doc.object(), &normalized, &why)) {
        return error(400, why);
    }
    if (!storage_->setSetting(EventSetting, normalized)) {
        return error(500, QStringLiteral("storage error"));
    }
    service_->setEventOverride(normalized);
    qCInfo(lcAdmin).noquote() << "event set to" << normalized;
    return event();
}

HttpResponse AdminService::clearEvent() {
    if (!storage_->removeSetting(EventSetting)) {
        return error(500, QStringLiteral("storage error"));
    }
    service_->setEventOverride(std::nullopt);
    qCInfo(lcAdmin) << "event cleared, using the config's event";
    return event();
}

HttpResponse AdminService::leaderboards(const QString& size) {
    int limit = 0;
    if (!size.isEmpty()) {
        bool ok = false;
        limit = size.toInt(&ok);
        if (!ok || limit < 0) {
            return error(400, QStringLiteral("bad size"));
        }
    }
    const QJsonObject boards = buildLeaderboards(*storage_, limit);
    if (boards.contains("error")) {
        return error(500, QStringLiteral("storage error"));
    }
    return json(200, boards);
}
