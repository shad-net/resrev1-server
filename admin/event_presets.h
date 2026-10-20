// SPDX-FileCopyrightText: Copyright 2026 Resident Evil Revelations Custom Server Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVector>

namespace Presets {

struct Preset {
    QString name;
    bool tested;
    QJsonObject event;
};

inline QJsonObject subjugation(int id, int bonus) {
    return QJsonObject{{"subjugation_enemy_id", QJsonArray{id}},
                       {"subjugation_exp", QJsonArray{bonus}}};
}

inline const QVector<Preset>& events() {
    static const QVector<Preset> presets = {
        {QStringLiteral("Ooze kill EXP +100 % (x2)"), true, subjugation(0, 1000)},
        {QStringLiteral("Ooze kill EXP +200 % (x3)"), true, subjugation(0, 2000)},
        {QStringLiteral("Ooze kill EXP +500 % (maximum)"), true, subjugation(0, 5000)},
        {QStringLiteral("All enemies 0-19 kill EXP +100 %"), true,
         [] {
             QJsonArray ids, bonus;
             for (int i = 0; i < 20; ++i) {
                 ids.append(i);
                 bonus.append(1000);
             }
             return QJsonObject{{"subjugation_enemy_id", ids}, {"subjugation_exp", bonus}};
         }()},
        {QStringLiteral("Double EXP on Chasm (special event 1)"), false,
         QJsonObject{
             {"special_event_id", 1}, {"special_serial_id", 1}, {"special_exp_rate", 1000}}},
        {QStringLiteral("Double EXP on Trench (special event 2)"), false,
         QJsonObject{
             {"special_event_id", 2}, {"special_serial_id", 1}, {"special_exp_rate", 1000}}},
        {QStringLiteral("Double EXP on Abyss (special event 3)"), false,
         QJsonObject{
             {"special_event_id", 3}, {"special_serial_id", 1}, {"special_exp_rate", 1000}}},
        {QStringLiteral("Wanted enemy on every stage (special event 8)"), false,
         QJsonObject{
             {"special_event_id", 8}, {"special_serial_id", 1}, {"special_wanted_rate", 1000}}},
        {QStringLiteral("Raid shop: weapons at half price"), false,
         QJsonObject{{"weekend_type", 1}, {"weekend_discount_rate", 500}}},
        {QStringLiteral("Raid shop: weapons at 80 % of the price"), false,
         QJsonObject{{"weekend_type", 1}, {"weekend_discount_rate", 800}}},
        {QStringLiteral("Wanted enemy on every stage, named RAIDSERVER"), false,
         QJsonObject{{"special_event_id", 8},
                     {"special_serial_id", 1},
                     {"special_wanted_rate", 1000},
                     {"collaboration_wanted_name", QJsonArray{QStringLiteral("RAIDSERVER")}},
                     {"collaboration_wanted_serial_id", QJsonArray{1}}}},
    };
    return presets;
}

// The preset's name with "(untested)" when its effect is not confirmed in game.
inline QString label(const Preset& p) {
    return p.tested ? p.name : p.name + QStringLiteral(" (untested)");
}

} // namespace Presets
