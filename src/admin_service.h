// SPDX-FileCopyrightText: Copyright 2026 Resident Evil Revelations Custom Server Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "http_server.h"

#include <QElapsedTimer>

class RevelationsService;
class Storage;

class AdminService {
public:
    AdminService(Storage* storage, RevelationsService* service);
    bool loadStored(QString* error);
    void handle(const HttpRequest& request, HttpServer::Responder respond);

private:
    HttpResponse route(const HttpRequest& request);
    HttpResponse status();
    HttpResponse players();
    HttpResponse upload(qint64 playerId);
    HttpResponse stats(qint64 playerId, const QString& uploadId);
    HttpResponse history(qint64 playerId);
    HttpResponse resetClaims(qint64 playerId, const QByteArray& body);
    HttpResponse rewards();
    HttpResponse addReward(const QByteArray& body);
    HttpResponse removeReward(qint64 serial);
    HttpResponse event();
    HttpResponse setEvent(const QByteArray& body);
    HttpResponse clearEvent();
    HttpResponse leaderboards(const QString& size);
    HttpResponse schedule();
    HttpResponse setSchedule(const QByteArray& body);

    Storage* storage_;
    RevelationsService* service_;
    QElapsedTimer uptime_;
};
