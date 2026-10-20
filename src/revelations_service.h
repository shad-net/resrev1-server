// SPDX-FileCopyrightText: Copyright 2026 Resident Evil Revelations Custom Server Project
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "config.h"
#include "http_server.h"
#include "shadnet_verifier.h"
#include "storage.h"

#include <QJsonObject>

#include <memory>

class Blowfish;

class RevelationsService {
public:
    RevelationsService(Storage* storage, ServerConfig config);
    ~RevelationsService();

    void setConfig(ServerConfig config);
    const ServerConfig& config() const {
        return config_;
    }
    void setStoredRewards(QVector<Reward> rewards);
    const QVector<Reward>& storedRewards() const {
        return stored_;
    }
    const QVector<Reward>& rewards() const {
        return merged_;
    }
    void setEventOverride(std::optional<QByteArray> eventJson) {
        eventOverride_ = std::move(eventJson);
    }
    const std::optional<QByteArray>& eventOverride() const {
        return eventOverride_;
    }
    const QByteArray& eventJson() const {
        return eventOverride_ ? *eventOverride_ : config_.eventJson;
    }
    void handle(const HttpRequest& request, HttpServer::Responder respond);

private:
    HttpResponse reguc(const Player& player);
    HttpResponse reudw(const Player& player, const QByteArray& body);
    HttpResponse regrw(const Player& player, const HttpRequest& request, const QString& onlineId);
    HttpResponse redrw(const Player& player, const QByteArray& body, const QString& onlineId);
    HttpResponse regev();

    void serve(const QString& endpoint, const HttpRequest& request, const QString& identity,
               const QString& onlineId, const HttpServer::Responder& respond);
    HttpResponse denied(const QString& endpoint, int status, const QString& message) const;
    std::optional<QByteArray> decryptBody(const QByteArray& body) const;
    HttpResponse encrypted(const QByteArray& json) const;
    HttpResponse result(bool ok, const QString& message = {}) const;

    void mergeRewards();
    HttpResponse leaderboards(bool json);

    Storage* storage_;
    ServerConfig config_;
    QVector<Reward> stored_;
    QVector<Reward> merged_;
    std::optional<QByteArray> eventOverride_;
    std::unique_ptr<Blowfish> cipher_;
    ShadNetVerifier verifier_;
    QJsonObject boards_;
    qint64 boardsBuilt_ = 0;
};
