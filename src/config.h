// SPDX-FileCopyrightText: Copyright 2026 Resident Evil Revelations Custom Server Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "event_schedule.h"

#include <QByteArray>
#include <QHostAddress>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>

struct Reward {
    qint64 serial = 0;
    int type = 0;
    QVector<QPair<QString, qint64>> fields;
    std::optional<bool> weaponRandomTag;
    QStringList players;
    std::optional<TimeWindow> window;
    static constexpr qint64 WeeklySerials = 1000;
    qint64 lastSerial() const {
        return window && window->weekly ? serial + WeeklySerials - 1 : serial;
    }
    bool overlaps(const Reward& other) const {
        return serial <= other.lastSerial() && other.serial <= lastSerial();
    }
    std::optional<qint64> serialAt(const QDateTime& now) const {
        if (!window) {
            return serial;
        }
        const auto occurrence = occurrenceAt(*window, now);
        if (!occurrence) {
            return std::nullopt;
        }
        return window->weekly ? serial + weekNumber(*window, *occurrence) : serial;
    }

    bool offeredTo(const QString& onlineId) const {
        return players.isEmpty() || (!onlineId.isEmpty() && players.contains(onlineId.toLower()));
    }
};

struct ServerConfig {
    QHostAddress address = QHostAddress::Any;
    quint16 port = 8080;
    QString basePath = QStringLiteral("/residentevil/revelations/psn2");
    QString database = QStringLiteral("revelations.sqlite");
    QByteArray key = QByteArrayLiteral("FimDBBeCQ5tANLR0");
    QString shadnetUrl;
    QHostAddress adminAddress = QHostAddress::LocalHost;
    quint16 adminPort = 8081;
    QString adminKey;
    QString logFile;
    qint64 logMaxBytes = 10 * 1024 * 1024;
    bool leaderboardsPublic = false;
    int leaderboardSize = 10;
    QVector<Reward> rewards;
    QByteArray eventJson = QByteArrayLiteral("{}");
    QVector<ScheduledEvent> eventSchedule;
};

std::optional<ServerConfig> loadConfig(const QByteArray& json, QString* error);
QByteArray defaultConfigJson();
bool parseReward(const QJsonObject& obj, const QString& where, Reward* out, QString* error);
bool parseEventJson(const QJsonValue& value, QByteArray* out, QString* error);
QJsonObject rewardToJson(const Reward& reward);
