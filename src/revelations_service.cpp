// SPDX-FileCopyrightText: Copyright 2026 Resident Evil Revelations Custom Server Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "revelations_service.h"

#include "blowfish.h"
#include "json_writer.h"

#include <algorithm>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>

namespace {

Q_LOGGING_CATEGORY(lcService, "revelations")

constexpr int MaxRewardsPerRequest = 20;
constexpr qsizetype TssMaxSize = 0x100;
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
}

void RevelationsService::setStoredRewards(QVector<Reward> rewards) {
    stored_ = std::move(rewards);
    mergeRewards();
}

void RevelationsService::mergeRewards() {
    merged_ = config_.rewards;
    QSet<qint64> serials;
    for (const Reward& r : config_.rewards) {
        serials.insert(r.serial);
    }
    for (const Reward& r : stored_) {
        if (serials.contains(r.serial)) {
            qCWarning(lcService) << "admin reward" << r.serial
                                 << "is hidden by the config reward with the same serial";
            continue;
        }
        merged_.append(r);
    }
}

void RevelationsService::handle(const HttpRequest& request, HttpServer::Responder respond) {
    QString path = request.path;
    if (!config_.basePath.isEmpty() && path.startsWith(config_.basePath + '/')) {
        path = path.mid(config_.basePath.size());
    }
    const QString endpoint = path.section('/', -1);
    const QString ip = peerString(request.peer);

    static const QStringList endpoints = {"reguc.php", "reudw.php", "regrw.php", "redrw.php",
                                          "regev.php"};
    if (!endpoints.contains(endpoint)) {
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

    verifier_.verify(code, [this, endpoint, request, ip,
                            respond](const ShadNetVerifier::Result& result) {
        switch (result.status) {
        case ShadNetVerifier::Status::Verified:
            if (result.clientId != QLatin1String(ShadNetVerifier::GameClientId)) {
                respond(denied(endpoint, 403,
                               QStringLiteral("authorization code is for another client")));
                return;
            }
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
    // reguc, reudw and redrw report failures in their body, regrw and regev have no result field,
    // so a non-200 status is the only way to make the game treat the call as failed.
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
    JsonWriter w;
    w.beginArray();
    int count = 0;
    for (const Reward& reward : merged_) {
        if (count == maxNum) {
            break;
        }
        if (claimed.contains(reward.serial) || !reward.offeredTo(onlineId)) {
            continue;
        }
        w.beginObject().key("serial").value(reward.serial).key("type").value(qint64(reward.type));
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
    for (const Reward& reward : merged_) {
        if (reward.offeredTo(onlineId)) {
            offered.insert(reward.serial);
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

HttpResponse RevelationsService::regev() {
    return encrypted(eventJson());
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
