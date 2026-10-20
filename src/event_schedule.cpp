// SPDX-FileCopyrightText: Copyright 2026 Resident Evil Revelations Custom Server Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "event_schedule.h"

#include "config.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>

#include <algorithm>

namespace {

bool fail(QString* error, const QString& message) {
    if (error) {
        *error = message;
    }
    return false;
}

QString percent(double value) {
    return QString::number(value, 'g', 4);
}

QString enemyName(int id) {
    static const char* models[] = {"em1000", "em1010", "em1020", "em1030", "em1060",
                                   "em1210", "em1230", "em1220", "em1200", "em1300",
                                   "em1310", "em1400", "em1410", "em1240", "em1420",
                                   "em1440", "em1211", "em1221", "em1401", "em1422"};
    if (id == 0) {
        return QStringLiteral("Oozes");
    }
    if (id >= 0 && id < 20) {
        return QStringLiteral("enemy %1 (%2)").arg(id).arg(QLatin1String(models[id]));
    }
    return QStringLiteral("enemy %1").arg(id);
}

} // namespace

bool parseEventSchedule(const QJsonValue& value, QVector<ScheduledEvent>* out, QString* error,
                        const QString& name) {
    out->clear();
    if (value.isUndefined() || value.isNull()) {
        return true;
    }
    if (!value.isArray()) {
        return fail(error, name + QStringLiteral(" must be an array"));
    }
    const QJsonArray entries = value.toArray();
    for (int i = 0; i < entries.size(); ++i) {
        const QString where = QStringLiteral("%1[%2]").arg(name).arg(i);
        if (!entries[i].isObject()) {
            return fail(error, where + QStringLiteral(" must be an object"));
        }
        const QJsonObject o = entries[i].toObject();
        ScheduledEvent e;
        e.name = o.value("name").toString().trimmed();
        if (e.name.isEmpty()) {
            return fail(error, where + QStringLiteral(".name is required"));
        }
        e.description = o.value("description").toString();
        if (!parseTimeWindow(o, where, &e.window, error)) {
            return false;
        }
        QString why;
        if (!parseEventJson(o.value("event"), &e.eventJson, &why)) {
            return fail(error, where + QStringLiteral(": ") + why);
        }
        if (e.eventJson == "{}") {
            return fail(error, where + QStringLiteral(".event is empty"));
        }
        out->append(e);
    }
    return true;
}

QJsonObject scheduledEventToJson(const ScheduledEvent& e) {
    QJsonObject o = timeWindowToJson(e.window);
    o.insert("name", e.name);
    if (!e.description.isEmpty()) {
        o.insert("description", e.description);
    }
    o.insert("event", QJsonDocument::fromJson(e.eventJson).object());
    return o;
}

std::optional<EventWindow> activeWindow(const QVector<ScheduledEvent>& schedule,
                                        const QDateTime& now) {
    for (const ScheduledEvent& e : schedule) {
        if (const auto o = occurrenceAt(e.window, now)) {
            return EventWindow{&e, o->start, o->end};
        }
    }
    return std::nullopt;
}

QVector<EventWindow> upcomingWindows(const QVector<ScheduledEvent>& schedule, const QDateTime& now,
                                     int count) {
    QVector<EventWindow> out;
    for (const ScheduledEvent& e : schedule) {
        if (const auto o = nextOccurrence(e.window, now)) {
            out.append(EventWindow{&e, o->start, o->end});
        }
    }
    std::sort(out.begin(), out.end(),
              [](const EventWindow& a, const EventWindow& b) { return a.start < b.start; });
    if (out.size() > count) {
        out.resize(count);
    }
    return out;
}

QStringList describeEvent(const QByteArray& eventJson) {
    const QJsonObject e = QJsonDocument::fromJson(eventJson).object();
    QStringList lines;
    const QJsonArray ids = e.value("subjugation_enemy_id").toArray();
    const QJsonArray exp = e.value("subjugation_exp").toArray();
    QMap<double, QStringList> byBonus;
    for (int i = 0; i < ids.size() && i < exp.size(); ++i) {
        const int id = ids[i].toInt();
        const double bonus = std::min(exp[i].toDouble() / 10.0, 500.0);
        if (bonus <= 0 || id % 21 == 20 || id > 104) {
            continue;
        }
        const QString name = id < 21 ? enemyName(id) : QStringLiteral("enemy %1").arg(id);
        if (!byBonus[bonus].contains(name)) {
            byBonus[bonus].append(name);
        }
    }
    for (auto it = byBonus.constEnd(); it != byBonus.constBegin();) {
        --it;
        lines << QStringLiteral("+%1 % EXP for defeating %2")
                     .arg(percent(it.key()), it.value().join(QStringLiteral(", ")));
    }

    const auto priceShare = [&e](const char* key) {
        return percent(std::clamp(e.value(QLatin1String(key)).toDouble() / 10.0, 0.0, 100.0));
    };
    const int weekendType = e.value("weekend_type").toInt(0);
    if ((weekendType == 1 || weekendType == 3 || weekendType == 5) &&
        e.contains("weekend_discount_rate")) {
        static const char* groups[] = {"", "weapons", "", "item group X", "", "item group Y"};
        lines << QStringLiteral("Raid shop: %1 at %2 % of the price")
                     .arg(QLatin1String(groups[weekendType]), priceShare("weekend_discount_rate"));
    } else if (weekendType == 2 || weekendType == 4) {
        const int items = int(e.value("weekend_item_item_id").toArray().size());
        if (items > 0) {
            lines << (items == 1
                          ? QStringLiteral("Special price on 1 Raid shop item")
                          : QStringLiteral("Special prices on %1 Raid shop items").arg(items));
        }
    }

    const int special = e.value("special_event_id").toInt(0);
    static const char* difficulties[] = {"Chasm", "Trench", "Abyss"};
    if (special >= 1 && special <= 3 && e.value("special_exp_rate").toDouble() > 0) {
        lines << QStringLiteral("+%1 % EXP on %2")
                     .arg(percent(std::min(e.value("special_exp_rate").toDouble() / 10.0, 500.0)),
                          QLatin1String(difficulties[special - 1]));
    }
    if (special == 8 && e.value("special_wanted_rate").toDouble() > 0) {
        const double chance = std::min(e.value("special_wanted_rate").toDouble() / 10.0, 100.0);
        lines << (chance >= 100
                      ? QStringLiteral("A Wanted enemy on every stage")
                      : QStringLiteral("+%1 % chance of a Wanted enemy").arg(percent(chance)));
    }
    const int discount = e.value("discount_event_id").toInt(0);
    if ((discount == 1 || discount == 2) && e.contains("discount_exp_rate")) {
        lines << QStringLiteral("Raid shop: item group %1 at %2 % of the price")
                     .arg(discount == 1 ? QStringLiteral("A") : QStringLiteral("B"),
                          priceShare("discount_exp_rate"));
    }

    QStringList names;
    for (const QJsonValue& n : e.value("collaboration_wanted_name").toArray()) {
        if (!n.toString().isEmpty()) {
            names << n.toString();
        }
    }
    if (!names.isEmpty()) {
        lines << QStringLiteral("Wanted enemies named %1").arg(names.join(QStringLiteral(", ")));
    }
    return lines;
}
