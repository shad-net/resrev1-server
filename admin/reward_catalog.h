// SPDX-FileCopyrightText: Copyright 2026 Resident Evil Revelations Custom Server Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace Catalog {

inline const QStringList& reliefNames() {
    static const QStringList names = {
        "Handgun Ammo", "Shotgun Shells", "Machine Gun Ammo", "Rifle Ammo",   "Magnum Ammo",
        "Hand Grenade", "Pulse Grenade",  "Shock Grenade",    "B.O.W. Decoy", "Green Herb",
    };
    return names;
}

inline const QStringList& partNames() {
    static const QStringList names = {
        "Damage 1",        "Damage 2",        "Damage 3",        "Damage 4",
        "Damage 5",        "Damage 6",        "Fire Rate 1",     "Fire Rate 2",
        "Fire Rate 3",     "Fire Rate 4",     "Fire Rate 5",     "Long Magazine 1",
        "Long Magazine 2", "Long Magazine 3", "Long Magazine 4", "Long Magazine 5",
        "Bind 1",          "Bind 2",          "Daze 1",          "Daze 2",
        "Critical 1",      "Burst 1",         "Burst 2",         "Infighter 1",
        "Narrow 1",        "Charge Shot 1",   "Charge Shot 2",   "Charge Shot 3",
        "Auto Loader 1",   "Edge Runner 1",   "Damage 7",        "Long Magazine 6",
        "Daze 3",          "Critical 2",      "Burst 3",         "Burst 4",
        "Burst 5",         "Burst 6",         "Edge Runner 2",   "Infighter 2",
        "Infighter 3",     "Narrow 2",        "Charge Shot 4",   "Brass Knuckle 1",
        "Reloader 1",      "Reloader 2",
    };
    return names;
}

// Names marked * are matched by weapon class and not yet seen in game.
inline const QStringList& weaponNames() {
    static const QStringList names = {
        "M92F",  "Government", "G18",   "PC356", "Python",        "L. Hawk", "Pale Rider *",
        "MP5",   "P-90",       "AUG",   "G36",   "High Roller *", "Windham", "M3",
        "Hydra", "Drake *",    "M40A1", "PSG1",  "Muramasa *",
    };
    return names;
}

// Raid Mode shop items (msg/ps4/shop_item_eng, goods_name_<id>). The IDs have gaps.
struct NamedId {
    int id;
    const char* name;
};

inline const QVector<NamedId>& shopItems() {
    static const QVector<NamedId> items = {
        {0, "Body Armor 1"},
        {1, "Body Armor 2"},
        {2, "Body Armor 2-A"},
        {3, "Body Armor 3"},
        {4, "Body Armor 3-A"},
        {5, "Body Armor 4"},
        {6, "Body Armor 4-A"},
        {7, "Body Armor 5"},
        {8, "Body Armor 5-A"},
        {9, "Body Armor 6"},
        {10, "Handgun Ammo Bag 1"},
        {11, "Handgun Ammo Bag 2"},
        {12, "Handgun Ammo Bag 3"},
        {13, "Handgun Ammo Bag 4"},
        {14, "Handgun Ammo Bag 5"},
        {15, "Shotgun Ammo Bag 1"},
        {16, "Shotgun Ammo Bag 2"},
        {17, "Shotgun Ammo Bag 3"},
        {18, "Shotgun Ammo Bag 4"},
        {19, "Shotgun Ammo Bag 5"},
        {20, "Machine Gun Ammo Bag 1"},
        {21, "Machine Gun Ammo Bag 2"},
        {22, "Machine Gun Ammo Bag 3"},
        {23, "Machine Gun Ammo Bag 4"},
        {24, "Machine Gun Ammo Bag 5"},
        {25, "Rifle Ammo Bag 1"},
        {26, "Rifle Ammo Bag 2"},
        {27, "Rifle Ammo Bag 3"},
        {28, "Rifle Ammo Bag 4"},
        {29, "Rifle Ammo Bag 5"},
        {30, "Magnum Ammo Bag 1"},
        {31, "Magnum Ammo Bag 2"},
        {32, "Magnum Ammo Bag 3"},
        {33, "Magnum Ammo Bag 4"},
        {34, "Magnum Ammo Bag 5"},
        {35, "Grenade Holder 1"},
        {36, "Grenade Holder 2"},
        {37, "Grenade Holder 3"},
        {38, "Grenade Holder 4"},
        {39, "Grenade Holder 5"},
        {40, "Decoy Holder 1"},
        {41, "Decoy Holder 2"},
        {42, "Decoy Holder 3"},
        {43, "Decoy Holder 4"},
        {44, "Decoy Holder 5"},
        {45, "Shock Grenade Holder 1"},
        {46, "Shock Grenade Holder 2"},
        {47, "Shock Grenade Holder 3"},
        {48, "Shock Grenade Holder 4"},
        {49, "Shock Grenade Holder 5"},
        {50, "Pulse Grenade Holder 1"},
        {51, "Pulse Grenade Holder 2"},
        {52, "Pulse Grenade Holder 3"},
        {53, "Pulse Grenade Holder 4"},
        {54, "Pulse Grenade Holder 5"},
        {55, "Herb Case 1"},
        {56, "Herb Case 2"},
        {57, "Herb Case 3"},
        {58, "Herb Case 4"},
        {59, "Herb Case 5"},
        {60, "Additional Storage 1"},
        {61, "Additional Storage 2"},
        {62, "Additional Storage 3"},
        {63, "Additional Storage 4"},
        {69, "Fully charged handgun"},
        {70, "Fully charged shotgun"},
        {71, "Fully charged machine gun"},
        {72, "Fully charged rifle"},
        {73, "Fully charged magnum"},
        {74, "Fully charged grenade"},
        {75, "Fully charged decoy"},
        {76, "Fully charged shock grenade"},
        {77, "Fully charged pulse grenade"},
        {78, "Fully charged herb"},
        {80, "Replenish Consumables"},
        {81, "Firing Rate Boost A"},
        {82, "Firing Rate Boost B"},
        {83, "Firing Rate Boost C"},
        {84, "Firing Rate Boost D"},
        {85, "Firing Rate Boost E"},
        {86, "Capacity Boost A"},
        {87, "Capacity Boost B"},
        {88, "Capacity Boost C"},
        {89, "Capacity Boost D"},
        {90, "Capacity Boost E"},
        {91, "Firepower Boost A"},
        {92, "Firepower Boost B"},
        {93, "Firepower Boost C"},
        {94, "Firepower Boost D"},
        {95, "Firepower Boost E"},
        {98, "Change Tag to Short Range"},
        {99, "Change Tag to Long Range"},
        {100, "Change Tag to Easy Grip"},
        {101, "Change Tag to Speed Shot"},
        {102, "Change Tag to Steady Hand"},
        {103, "Change Tag to Speed Load"},
        {104, "Change Tag to Sonic Assist"},
        {105, "Change Tag to Light Weight"},
        {106, "Change Tag to Sonic Assist+"},
        {107, "Change Tag to Short Range+"},
        {108, "Change Tag to Long Range+"},
        {109, "Additional Storage 5"},
        {110, "Additional Storage 6"},
        {111, "Additional Storage 7"},
        {112, "Additional Storage 8"},
    };
    return items;
}

// Raid Mode enemies in the order of game/rank/CoopEnemyPoint (enemy ID 0x8003 + model code).
// Whether subjugation_enemy_id indexes this list is not confirmed yet, the game has no enemy names.
inline const QStringList& raidEnemies() {
    static const QStringList models = {"em1000", "em1010", "em1020", "em1030", "em1060", "em1200",
                                       "em1210", "em1211", "em1220", "em1230", "em1240", "em1300",
                                       "em1310", "em1400", "em1401", "em1420", "em1440", "em10a0",
                                       "em1221", "em1420", "em1250", "em1450"};
    return models;
}

inline QString shopItemName(qint64 id) {
    for (const NamedId& item : shopItems()) {
        if (item.id == id) {
            return QString::fromLatin1(item.name);
        }
    }
    return {};
}

inline QString nameAt(const QStringList& list, qint64 i) {
    return i >= 0 && i < list.size() ? list[int(i)] : QStringLiteral("#%1").arg(i);
}

inline QString describe(const QJsonObject& r) {
    const QString type = r.value("type").toString();
    if (type == QLatin1String("relief")) {
        return QStringLiteral("%1 x%2")
            .arg(nameAt(reliefNames(), r.value("relief_type").toInteger()))
            .arg(r.value("relief_count").toInteger());
    }
    if (type == QLatin1String("parts")) {
        return nameAt(partNames(), r.value("parts_id").toInteger());
    }
    if (type == QLatin1String("weapon")) {
        QString s = QStringLiteral("%1 Lv.%2, slot %3")
                        .arg(nameAt(weaponNames(), r.value("weapon_id").toInteger()))
                        .arg(r.value("weapon_rarity").toInteger())
                        .arg(r.value("weapon_slot").toInteger());
        if (r.value("weapon_randomtag").toBool()) {
            s += QStringLiteral(", random tag");
        }
        return s;
    }
    if (type == QLatin1String("gacha")) {
        return QStringLiteral("Random part, type %1 grade %2")
            .arg(r.value("gacha_type").toInteger())
            .arg(r.value("gacha_grade").toInteger());
    }
    return type;
}

} // namespace Catalog
