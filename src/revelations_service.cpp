// SPDX-FileCopyrightText: Copyright 2026 Resident Evil Revelations Custom Server Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "revelations_service.h"

#include "blowfish.h"
#include "json_writer.h"
#include "leaderboard.h"
#include "traffic.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>

#include <algorithm>

namespace {

Q_LOGGING_CATEGORY(lcService, "revelations")

constexpr int MaxRewardsPerRequest = 20;
constexpr qsizetype SaveBlobSize = 0x45c0;

QString peerString(const QHostAddress& address) {
    bool isV4 = false;
    const quint32 v4 = address.toIPv4Address(&isV4);
    return isV4 ? QHostAddress(v4).toString() : address.toString();
}

QByteArray stripNulPadding(QByteArray data) {
    while (data.endsWith('\0')) {
        data.chop(1);
    }
    return data;
}

const QStringList& gameEndpoints() {
    static const QStringList endpoints = {"reguc.php", "reudw.php", "regrw.php", "redrw.php",
                                          "regev.php"};
    return endpoints;
}

QString headerLines(const HttpRequest& request) {
    QList<QByteArray> names = request.headers.keys();
    std::sort(names.begin(), names.end());
    QStringList lines;
    for (const QByteArray& name : names) {
        QString value = QString::fromLatin1(request.headers.value(name));
        if (name == "x-np-authcode") {
            const qsizetype comma = value.indexOf(',');
            const QString code = comma < 0 ? value : value.left(comma);
            if (code.size() > 6) {
                value = code.left(4) + QStringLiteral("...") +
                        (comma < 0 ? QString() : value.mid(comma));
            }
        }
        lines << QString::fromLatin1(name) + QStringLiteral(": ") + value;
    }
    return lines.join('\n');
}

} // namespace

RevelationsService::RevelationsService(Storage* storage, ServerConfig config) : storage_(storage) {
    setConfig(std::move(config));
}

RevelationsService::~RevelationsService() = default;

void RevelationsService::setConfig(ServerConfig config) {
    config_ = std::move(config);
    cipher_ = std::make_unique<Blowfish>(config_.key);
    verifier_.setEndpoint(config_.shadnetUrl);
    mergeRewards();
    schedule_ = adminSchedule_ + config_.eventSchedule;
}

void RevelationsService::setStoredRewards(QVector<Reward> rewards) {
    stored_ = std::move(rewards);
    mergeRewards();
}

void RevelationsService::setAdminSchedule(QVector<ScheduledEvent> schedule) {
    adminSchedule_ = std::move(schedule);
    schedule_ = adminSchedule_ + config_.eventSchedule;
}

void RevelationsService::mergeRewards() {
    merged_ = config_.rewards;
    for (const Reward& r : stored_) {
        const bool hidden = std::any_of(config_.rewards.begin(), config_.rewards.end(),
                                        [&r](const Reward& c) { return c.overlaps(r); });
        if (hidden) {
            qCWarning(lcService) << "admin reward" << r.serial
                                 << "is hidden by a config reward using the same serial";
            continue;
        }
        merged_.append(r);
    }
}

void RevelationsService::handle(const HttpRequest& request, HttpServer::Responder reply) {
    QString path = request.path;
    if (!config_.basePath.isEmpty() && path.startsWith(config_.basePath + '/')) {
        path = path.mid(config_.basePath.size());
    }
    const QString endpoint = path.section('/', -1);
    const QString ip = peerString(request.peer);

    const bool game = gameEndpoints().contains(endpoint);
    auto entry = std::make_shared<TrafficEntry>();
    entry->id = Traffic::nextId();
    entry->time = QDateTime::currentMSecsSinceEpoch();
    entry->method = QString::fromLatin1(request.method);
    entry->path =
        request.path + (request.query.isEmpty() ? QString() : '?' + request.query.toString());
    entry->endpoint = game ? endpoint
                      : request.path.startsWith(QLatin1String("/leaderboards"))
                          ? QStringLiteral("leaderboards")
                          : endpoint;
    entry->peer = ip;
    entry->headers = headerLines(request);
    entry->bytesIn = request.body.size();
    QByteArray requestPlain = request.body;
    if (game && !request.body.isEmpty()) {
        const auto plain = cipher_->decrypt(request.body);
        requestPlain =
            plain ? *plain : QByteArrayLiteral("(not a whole number of Blowfish blocks)");
    }
    entry->request = Traffic::keep(requestPlain);
    QElapsedTimer timer;
    timer.start();
    const HttpServer::Responder respond = [this, entry, timer, game, reply,
                                           requestPlain](HttpResponse response) {
        entry->status = response.status;
        entry->elapsedMs = timer.elapsed();
        entry->bytesOut = response.body.size();
        QByteArray responsePlain = response.body;
        if (game && response.status == 200 && !response.body.isEmpty()) {
            if (const auto plain = cipher_->decrypt(response.body)) {
                responsePlain = *plain;
                entry->result = Traffic::resultOf(*plain);
            }
        } else if (response.contentType.startsWith("text/html")) {
            responsePlain = QStringLiteral("(%1-byte web page)").arg(response.body.size()).toUtf8();
        }
        entry->response = Traffic::keep(responsePlain);
        Traffic::record(*entry);
        if (config_.trace) {
            Traffic::trace(*entry, requestPlain, responsePlain, config_.traceBodyBytes);
        }
        reply(std::move(response));
    };

    if (config_.leaderboardsPublic && request.method == "GET" &&
        (request.path == QLatin1String("/leaderboards") ||
         request.path == QLatin1String("/leaderboards/") ||
         request.path == QLatin1String("/leaderboards.json"))) {
        const HttpResponse response = leaderboards(request.path.endsWith(QLatin1String(".json")));
        qCInfo(lcService).noquote()
            << request.method << request.path << "from" << ip << "->" << response.status;
        respond(response);
        return;
    }

    if (!game) {
        qCInfo(lcService).noquote() << request.method << request.path << "from" << ip << "-> 404";
        HttpResponse notFound;
        notFound.status = 404;
        notFound.contentType = "text/plain";
        notFound.body = "Not Found";
        respond(notFound);
        return;
    }

    const QByteArray header = request.headers.value("x-np-authcode");
    const qsizetype comma = header.indexOf(',');
    const QString code = QString::fromLatin1(comma < 0 ? header : header.left(comma)).trimmed();
    const bool placeholder = code.isEmpty() || code == QLatin1String("AUTHEN");

    // Every player is a shadNet account: requests without a real code are refused.
    if (placeholder) {
        qCWarning(lcService).noquote()
            << endpoint << "from" << ip << "rejected: no shadNet authorization code";
        respond(denied(endpoint, 403, QStringLiteral("not signed in to shadNet")));
        return;
    }

    verifier_.verify(code, [this, endpoint, request, ip, respond,
                            entry](const ShadNetVerifier::Result& result) {
        switch (result.status) {
        case ShadNetVerifier::Status::Verified:
            if (result.clientId != QLatin1String(ShadNetVerifier::GameClientId)) {
                respond(denied(endpoint, 403,
                               QStringLiteral("authorization code is for another client")));
                return;
            }
            entry->player = result.onlineId;
            serve(endpoint, request, QStringLiteral("account:") + result.accountId, result.onlineId,
                  respond);
            return;
        case ShadNetVerifier::Status::Rejected:
            qCWarning(lcService).noquote()
                << endpoint << "from" << ip << "rejected by shadNet:" << result.message;
            respond(denied(endpoint, 403, QStringLiteral("authorization code rejected")));
            return;
        case ShadNetVerifier::Status::Unavailable:
            qCWarning(lcService).noquote()
                << endpoint << "from" << ip << "shadNet unavailable:" << result.message;
            respond(denied(endpoint, 503, QStringLiteral("authentication service unavailable")));
            return;
        }
    });
}

void RevelationsService::serve(const QString& endpoint, const HttpRequest& request,
                               const QString& identity, const QString& onlineId,
                               const HttpServer::Responder& respond) {
    const auto player = storage_->player(identity, onlineId);
    if (!player) {
        qCWarning(lcService).noquote() << "could not load or create player" << identity;
        respond(denied(endpoint, 500, QStringLiteral("storage error")));
        return;
    }
    qCInfo(lcService).noquote() << request.method << endpoint << "player" << player->code
                                << "(" + identity +
                                       (onlineId.isEmpty() ? QString() : " " + onlineId) + ")"
                                << "body" << request.body.size() << "bytes";

    if (endpoint == QLatin1String("reguc.php")) {
        respond(reguc(*player));
    } else if (endpoint == QLatin1String("reudw.php")) {
        respond(reudw(*player, request.body));
    } else if (endpoint == QLatin1String("regrw.php")) {
        respond(regrw(*player, request, onlineId));
    } else if (endpoint == QLatin1String("redrw.php")) {
        respond(redrw(*player, request.body, onlineId));
    } else {
        respond(regev());
    }
}

HttpResponse RevelationsService::denied(const QString& endpoint, int status,
                                        const QString& message) const {
    if (endpoint == QLatin1String("regrw.php") || endpoint == QLatin1String("regev.php")) {
        HttpResponse response;
        response.status = status;
        response.contentType = "text/plain";
        response.body = message.toUtf8();
        return response;
    }
    return result(false, message);
}

HttpResponse RevelationsService::reguc(const Player& player) {
    JsonWriter w;
    w.beginObject().key("result").value("OK").key("code").value(player.code).endObject();
    return encrypted(w.data());
}

HttpResponse RevelationsService::reudw(const Player& player, const QByteArray& body) {
    const auto data = decryptBody(body);
    if (!data || data->isEmpty()) {
        return result(false, QStringLiteral("invalid upload"));
    }
    if (data->size() != SaveBlobSize) {
        qCInfo(lcService) << "upload is" << data->size() << "bytes, expected" << SaveBlobSize;
    }
    if (!storage_->storeUpload(player.id, *data)) {
        return result(false, QStringLiteral("storage error"));
    }
    return result(true);
}

HttpResponse RevelationsService::regrw(const Player& player, const HttpRequest& request,
                                       const QString& onlineId) {
    bool ok = false;
    int maxNum = request.query.queryItemValue(QStringLiteral("max_num")).toInt(&ok);
    if (!ok) {
        maxNum = MaxRewardsPerRequest;
    }
    maxNum = std::clamp(maxNum, 0, MaxRewardsPerRequest);

    const QSet<qint64> claimed = storage_->claimedSerials(player.id);
    const QDateTime now = QDateTime::currentDateTimeUtc();
    JsonWriter w;
    w.beginArray();
    int count = 0;
    for (const Reward& reward : merged_) {
        if (count == maxNum) {
            break;
        }
        const auto serial = reward.serialAt(now); // nullopt while its window is closed
        if (!serial || claimed.contains(*serial) || !reward.offeredTo(onlineId)) {
            continue;
        }
        w.beginObject().key("serial").value(*serial).key("type").value(qint64(reward.type));
        for (const auto& [name, value] : reward.fields) {
            w.key(name).value(value);
        }
        if (reward.weaponRandomTag) {
            w.key("weapon_randomtag").value(*reward.weaponRandomTag);
        }
        w.endObject();
        ++count;
    }
    w.endArray();
    qCInfo(lcService) << "sending" << count << "reward(s)";
    return encrypted(w.data());
}

HttpResponse RevelationsService::redrw(const Player& player, const QByteArray& body,
                                       const QString& onlineId) {
    const auto data = decryptBody(body);
    if (!data) {
        return result(false, QStringLiteral("invalid body"));
    }
    const QJsonDocument doc = QJsonDocument::fromJson(stripNulPadding(*data));
    const QJsonValue serials = doc.object().value(QStringLiteral("serial"));
    if (!serials.isArray()) {
        return result(false, QStringLiteral("missing serial list"));
    }
    QSet<qint64> offered;
    const QDateTime now = QDateTime::currentDateTimeUtc();
    for (const Reward& reward : merged_) {
        const auto serial = reward.serialAt(now);
        if (serial && reward.offeredTo(onlineId)) {
            offered.insert(*serial);
        }
    }
    const QSet<qint64> already = storage_->claimedSerials(player.id);
    QSet<qint64> fresh;
    QList<qint64> repeats;
    QList<qint64> unknown;
    for (const QJsonValue& v : serials.toArray()) {
        const qint64 serial = v.isDouble() ? static_cast<qint64>(v.toDouble()) : 0;
        if (serial == 0) {
            continue;
        }
        if (already.contains(serial)) {
            repeats.append(serial);
        } else if (offered.contains(serial)) {
            fresh.insert(serial);
        } else {
            unknown.append(serial);
        }
    }
    if (!fresh.isEmpty() && !storage_->claim(player.id, fresh)) {
        return result(false, QStringLiteral("storage error"));
    }
    QList<qint64> freshList(fresh.begin(), fresh.end());
    std::sort(freshList.begin(), freshList.end());
    std::sort(unknown.begin(), unknown.end());
    QString line = QStringLiteral("claim: %1 new").arg(freshList.size());
    if (!freshList.isEmpty()) {
        QStringList ids;
        for (qint64 s : freshList) {
            ids << QString::number(s);
        }
        line += QStringLiteral(" (%1)").arg(ids.join(QStringLiteral(", ")));
    }
    line += QStringLiteral(", %1 already claimed").arg(repeats.size());
    if (!unknown.isEmpty()) {
        QStringList ids;
        for (qint64 s : unknown) {
            ids << QString::number(s);
        }
        line += QStringLiteral(", %1 not offered by this server (%2)")
                    .arg(unknown.size())
                    .arg(ids.join(QStringLiteral(", ")));
    }
    qCInfo(lcService).noquote() << line;
    return result(true);
}

RevelationsService::CurrentEvent RevelationsService::currentEvent() const {
    CurrentEvent current;
    if (eventOverride_) {
        current.json = *eventOverride_;
        current.source = QStringLiteral("admin");
    } else if (const auto window = activeWindow(schedule_, QDateTime::currentDateTimeUtc())) {
        current.json = window->entry->eventJson;
        current.source = QStringLiteral("schedule");
        current.window = window;
    } else {
        current.json = config_.eventJson;
        current.source = QStringLiteral("config");
    }
    const QString key =
        current.source + (current.window ? ':' + current.window->entry->name : QString());
    if (key != lastEventKey_) {
        if (!lastEventKey_.isEmpty()) {
            if (current.window) {
                qCInfo(lcService).noquote()
                    << "scheduled event" << current.window->entry->name << "started, until"
                    << current.window->end.toString(Qt::ISODate);
            } else {
                qCInfo(lcService).noquote()
                    << "event now from"
                    << (current.source == QLatin1String("admin") ? "the admin API" : "the config");
            }
        }
        lastEventKey_ = key;
    }
    return current;
}

QJsonObject RevelationsService::eventInfo(int upcoming) const {
    const CurrentEvent current = currentEvent();
    const auto lines = [](const QByteArray& json) {
        return QJsonArray::fromStringList(describeEvent(json));
    };
    QJsonObject now{
        {"source", current.source},
        {"description", lines(current.json)},
    };
    if (current.window) {
        now.insert("name", current.window->entry->name);
        now.insert("start", current.window->start.toSecsSinceEpoch());
        now.insert("end", current.window->end.toSecsSinceEpoch());
        if (!current.window->entry->description.isEmpty()) {
            now.insert("text", current.window->entry->description);
        }
    }
    QJsonArray next;
    for (const EventWindow& w :
         upcomingWindows(schedule_, QDateTime::currentDateTimeUtc(), upcoming)) {
        QJsonObject o{
            {"name", w.entry->name},
            {"start", w.start.toSecsSinceEpoch()},
            {"end", w.end.toSecsSinceEpoch()},
            {"description", lines(w.entry->eventJson)},
        };
        if (!w.entry->description.isEmpty()) {
            o.insert("text", w.entry->description);
        }
        next.append(o);
    }
    return QJsonObject{{"current", now}, {"upcoming", next}};
}

HttpResponse RevelationsService::regev() {
    const CurrentEvent current = currentEvent();
    const QByteArray& event = current.json;
    QString from =
        current.source == QLatin1String("admin") ? QStringLiteral("(set through the admin API)")
        : current.window
            ? QStringLiteral("(scheduled: %1, until %2)")
                  .arg(current.window->entry->name, current.window->end.toString(Qt::ISODate))
            : QStringLiteral("(from the config)");
    qCInfo(lcService).noquote() << "sending event"
                                << (event == "{}" ? QByteArray("{} (no event)") : event) << from;
    return encrypted(event);
}

std::optional<QByteArray> RevelationsService::decryptBody(const QByteArray& body) const {
    if (body.isEmpty()) {
        return QByteArray();
    }
    auto data = cipher_->decrypt(body);
    if (!data) {
        qCWarning(lcService) << "body of" << body.size()
                             << "bytes is not a whole number of Blowfish blocks";
    }
    return data;
}

HttpResponse RevelationsService::encrypted(const QByteArray& json) const {
    HttpResponse response;
    response.body = cipher_->encrypt(json, '\0');
    return response;
}

HttpResponse RevelationsService::result(bool ok, const QString& message) const {
    JsonWriter w;
    w.beginObject().key("result").value(ok ? "OK" : "NG");
    if (!message.isEmpty()) {
        w.key("message").value(message);
    }
    w.endObject();
    return encrypted(w.data());
}

HttpResponse RevelationsService::leaderboards(bool json) {
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    if (boards_.isEmpty() || now - boardsBuilt_ >= 60) {
        boards_ = buildLeaderboards(*storage_, config_.leaderboardSize);
        boardsBuilt_ = now;
    }
    HttpResponse response;
    if (boards_.contains("error")) {
        response.status = 500;
        response.contentType = "text/plain";
        response.body = "storage error";
        boards_ = {};
        return response;
    }

    QJsonObject page = boards_;
    page.insert("event", eventInfo(3));
    if (json) {
        response.contentType = "application/json";
        response.body = QJsonDocument(page).toJson(QJsonDocument::Compact);
    } else {
        response.contentType = "text/html; charset=utf-8";
        response.body = leaderboardsHtml(page);
    }
    return response;
}
