// SPDX-FileCopyrightText: Copyright 2026 Resident Evil Revelations Custom Server Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QTimeZone>

#include <optional>

// When something is available: once (start and end date-times) or every week (weekly.start and
// weekly.end such as "sat 00:00"), in a time zone. Used by scheduled events and rewards.
//
//   { "timezone": "Europe/Athens", "start": "2026-10-30T18:00:00", "end": "2026-11-02T00:00:00" }
//   { "timezone": "Europe/Athens", "weekly": { "start": "sat 00:00", "end": "mon 00:00" } }
struct TimeWindow {
    bool weekly = false;
    QDateTime start; // once
    QDateTime end;
    int weeklyStart = 0; // minutes from Monday 00:00
    int weeklyEnd = 0;
    QTimeZone timeZone = QTimeZone::utc();
};

struct Occurrence {
    QDateTime start;
    QDateTime end;
};

bool hasTimeWindow(const QJsonObject& o);
bool parseTimeWindow(const QJsonObject& o, const QString& where, TimeWindow* out, QString* error);
QJsonObject timeWindowToJson(const TimeWindow& w);
std::optional<Occurrence> occurrenceAt(const TimeWindow& w, const QDateTime& now);
std::optional<Occurrence> nextOccurrence(const TimeWindow& w, const QDateTime& now);
int weekNumber(const TimeWindow& w, const Occurrence& occurrence);
QString describeTimeWindow(const TimeWindow& w);
