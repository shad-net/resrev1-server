// SPDX-FileCopyrightText: Copyright 2026 Resident Evil Revelations Custom Server Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "time_window.h"

#include <QRegularExpression>
#include <QStringList>

namespace {

const QStringList& dayNames() {
    static const QStringList days = {"mon", "tue", "wed", "thu", "fri", "sat", "sun"};
    return days;
}

bool fail(QString* error, const QString& message) {
    if (error) {
        *error = message;
    }
    return false;
}

// "sat 00:00" -> minutes from Monday 00:00.
std::optional<int> parseWeekTime(const QString& text) {
    static const QRegularExpression re(
        QStringLiteral("^\\s*(mon|tue|wed|thu|fri|sat|sun)[a-z]*\\s+(\\d{1,2}):(\\d{2})\\s*$"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = re.match(text);
    if (!m.hasMatch()) {
        return std::nullopt;
    }
    const int day = int(dayNames().indexOf(m.captured(1).toLower()));
    const int hour = m.captured(2).toInt();
    const int minute = m.captured(3).toInt();
    if (hour > 23 || minute > 59) {
        return std::nullopt;
    }
    return (day * 24 + hour) * 60 + minute;
}

QString weekTimeText(int minutes) {
    return QStringLiteral("%1 %2:%3")
        .arg(dayNames()[minutes / (24 * 60)])
        .arg(minutes / 60 % 24, 2, 10, QLatin1Char('0'))
        .arg(minutes % 60, 2, 10, QLatin1Char('0'));
}

std::optional<QDateTime> parseDateTime(const QJsonValue& v, const QTimeZone& zone) {
    if (!v.isString()) {
        return std::nullopt;
    }
    QDateTime t = QDateTime::fromString(v.toString(), Qt::ISODate);
    if (!t.isValid()) {
        return std::nullopt;
    }
    // Without an offset the time is in the window's time zone.
    if (t.timeSpec() == Qt::LocalTime) {
        t = QDateTime(t.date(), t.time(), zone);
    }
    return t;
}

QDateTime wallTime(const QDate& monday, int minutes, const QTimeZone& zone) {
    return QDateTime(monday.addDays(minutes / (24 * 60)), QTime(minutes / 60 % 24, minutes % 60),
                     zone);
}

QDate mondayOf(const QDate& day) {
    return day.addDays(1 - day.dayOfWeek());
}

QVector<Occurrence> weeklyAround(const TimeWindow& w, const QDateTime& now) {
    const QDate monday = mondayOf(now.toTimeZone(w.timeZone).date());
    QVector<Occurrence> out;
    for (int week = -1; week <= 1; ++week) {
        const QDate base = monday.addDays(7 * week);
        out.append({wallTime(base, w.weeklyStart, w.timeZone),
                    wallTime(w.weeklyEnd > w.weeklyStart ? base : base.addDays(7), w.weeklyEnd,
                             w.timeZone)});
    }
    return out;
}

} // namespace

bool hasTimeWindow(const QJsonObject& o) {
    return o.contains("start") || o.contains("end") || o.contains("weekly");
}

bool parseTimeWindow(const QJsonObject& o, const QString& where, TimeWindow* out, QString* error) {
    TimeWindow w;
    if (o.contains("timezone")) {
        w.timeZone = QTimeZone(o.value("timezone").toString().toUtf8());
        if (!w.timeZone.isValid()) {
            return fail(error,
                        where +
                            QStringLiteral(
                                ".timezone is not a known time zone (such as \"Europe/Athens\")"));
        }
    }
    const bool once = o.contains("start") || o.contains("end");
    const bool weekly = o.contains("weekly");
    if (once == weekly) {
        return fail(error, where + QStringLiteral(" needs either start and end, or weekly"));
    }
    if (once) {
        const auto start = parseDateTime(o.value("start"), w.timeZone);
        const auto end = parseDateTime(o.value("end"), w.timeZone);
        if (!start || !end) {
            return fail(error,
                        where +
                            QStringLiteral(
                                ".start and .end must be dates such as \"2026-10-24T00:00:00\""));
        }
        if (*end <= *start) {
            return fail(error, where + QStringLiteral(".end must be after .start"));
        }
        w.start = *start;
        w.end = *end;
    } else {
        const QJsonObject week = o.value("weekly").toObject();
        const auto start = parseWeekTime(week.value("start").toString());
        const auto end = parseWeekTime(week.value("end").toString());
        if (!start || !end) {
            return fail(error,
                        where + QStringLiteral(
                                    ".weekly.start and .weekly.end must look like \"sat 00:00\""));
        }
        if (*start == *end) {
            return fail(error,
                        where + QStringLiteral(".weekly.end must differ from .weekly.start"));
        }
        w.weekly = true;
        w.weeklyStart = *start;
        w.weeklyEnd = *end;
    }
    *out = w;
    return true;
}

QJsonObject timeWindowToJson(const TimeWindow& w) {
    QJsonObject o;
    o.insert("timezone", QString::fromUtf8(w.timeZone.id()));
    if (w.weekly) {
        o.insert("weekly", QJsonObject{{"start", weekTimeText(w.weeklyStart)},
                                       {"end", weekTimeText(w.weeklyEnd)}});
    } else {
        o.insert("start", w.start.toTimeZone(w.timeZone).toString(Qt::ISODate));
        o.insert("end", w.end.toTimeZone(w.timeZone).toString(Qt::ISODate));
    }
    return o;
}

std::optional<Occurrence> occurrenceAt(const TimeWindow& w, const QDateTime& now) {
    if (!w.weekly) {
        if (w.start <= now && now < w.end) {
            return Occurrence{w.start, w.end};
        }
        return std::nullopt;
    }
    for (const Occurrence& o : weeklyAround(w, now)) {
        if (o.start <= now && now < o.end) {
            return o;
        }
    }
    return std::nullopt;
}

std::optional<Occurrence> nextOccurrence(const TimeWindow& w, const QDateTime& now) {
    if (!w.weekly) {
        if (w.start > now) {
            return Occurrence{w.start, w.end};
        }
        return std::nullopt;
    }
    for (const Occurrence& o : weeklyAround(w, now)) {
        if (o.start > now) {
            return o;
        }
    }
    return std::nullopt;
}

int weekNumber(const TimeWindow& w, const Occurrence& occurrence) {
    static const QDate epoch(2026, 1, 5); // a Monday
    const QDate monday = mondayOf(occurrence.start.toTimeZone(w.timeZone).date());
    return std::max(0, int(epoch.daysTo(monday) / 7));
}

QString describeTimeWindow(const TimeWindow& w) {
    const QString zone = QString::fromUtf8(w.timeZone.id());
    if (w.weekly) {
        const auto pretty = [](int minutes) {
            QString t = weekTimeText(minutes);
            t[0] = t[0].toUpper();
            return t;
        };
        return QStringLiteral("every %1 - %2 (%3)")
            .arg(pretty(w.weeklyStart), pretty(w.weeklyEnd), zone);
    }
    const QString format = QStringLiteral("yyyy-MM-dd HH:mm");
    return QStringLiteral("%1 - %2 (%3)")
        .arg(w.start.toTimeZone(w.timeZone).toString(format),
             w.end.toTimeZone(w.timeZone).toString(format), zone);
}
