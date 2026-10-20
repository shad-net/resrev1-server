// SPDX-FileCopyrightText: Copyright 2026 Resident Evil Revelations Custom Server Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "time_window.h"

#include <QByteArray>
#include <QDateTime>
#include <QJsonValue>
#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>

struct ScheduledEvent {
    QString name;
    QString description; // optional text for players; generated from the event when empty
    TimeWindow window;
    QByteArray eventJson; // validated like the config's "event"
};

// A window of a scheduled event: the entry and when this occurrence starts and ends.
struct EventWindow {
    const ScheduledEvent* entry = nullptr;
    QDateTime start;
    QDateTime end;
};

bool parseEventSchedule(const QJsonValue& value, QVector<ScheduledEvent>* out, QString* error,
                        const QString& where = QStringLiteral("event_schedule"));
QJsonObject scheduledEventToJson(const ScheduledEvent& e);
std::optional<EventWindow> activeWindow(const QVector<ScheduledEvent>& schedule,
                                        const QDateTime& now);
QVector<EventWindow> upcomingWindows(const QVector<ScheduledEvent>& schedule, const QDateTime& now,
                                     int count);
QStringList describeEvent(const QByteArray& eventJson);
