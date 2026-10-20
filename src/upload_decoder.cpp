// SPDX-FileCopyrightText: Copyright 2026 Resident Evil Revelations Custom Server Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "upload_decoder.h"

#include "leaderboard.h"

#include <QJsonArray>
#include <QString>
#include <QtEndian>

#include <algorithm>
#include <iterator>

namespace {

constexpr qsizetype UploadSize = 0x45c0;
constexpr qsizetype RecordSize = 24;

struct Table {
    const char* name;
    qsizetype offset;
    int count;
};

// Table A is probably the Campaign, table B the Raid Mode stages (B0 and B1 were Raid Stage 1
// and 2 on the first difficulty).
constexpr Table Tables[] = {
    {"A", 0x140, 78},
    {"B", 0xbd8, 62},
};

struct Counter {
    const char* name;
    qsizetype offset;
};

constexpr Counter Counters[] = {
    {"level", 0x1dc4},         {"exp_total", 0x1dc8},
    {"enemies_total", 0x1dcc}, {"clear_time_total", 0x1dd4},
    {"runs_total", 0x1334},
};

// Raid runs per character
constexpr qsizetype CharacterRunsOffset = 0x11a8;
constexpr qsizetype WantedOffset = 0x1de0;
constexpr qsizetype WantedSize = 0x28;
constexpr const char* Characters[] = {"Jill",    "Chris",   "Parker",    "Keith",       "Jessica",
                                      "Quint",   "Raymond", "O'Brian",   "Morgan",      "Norman",
                                      "Rachael", "HUNK",    "LADY HUNK", "Rachael Ooze"};
constexpr int CharacterCount = int(std::size(Characters));

bool inCharacterRuns(qsizetype offset) {
    return offset >= CharacterRunsOffset && offset < CharacterRunsOffset + CharacterCount * 4;
}

quint16 u16(const QByteArray& d, qsizetype offset) {
    return qFromLittleEndian<quint16>(d.constData() + offset);
}

quint32 u32(const QByteArray& d, qsizetype offset) {
    return qFromLittleEndian<quint32>(d.constData() + offset);
}

bool validUpload(const QByteArray& d) {
    return d.size() == UploadSize && d.startsWith("REBH");
}

QString fieldName(qsizetype offset) {
    if (offset < 16) {
        static const char* header[] = {"magic", "size", "version", "created"};
        return QLatin1String(header[offset / 4]);
    }
    for (const Counter& c : Counters) {
        if (c.offset == offset) {
            return QLatin1String(c.name);
        }
    }
    if (offset >= WantedOffset && offset < WantedOffset + 255 * WantedSize) {
        const qsizetype rel = (offset - WantedOffset) % WantedSize;
        const QString part = rel == 0     ? QStringLiteral("order")
                             : rel < 0x24 ? QStringLiteral("name")
                                          : QStringLiteral("kind");
        return QStringLiteral("wanted%1.%2").arg((offset - WantedOffset) / WantedSize).arg(part);
    }
    if (inCharacterRuns(offset)) {
        return QStringLiteral("runs.%1").arg(
            QLatin1String(Characters[(offset - CharacterRunsOffset) / 4]));
    }
    for (const Table& t : Tables) {
        if (offset >= t.offset && offset < t.offset + t.count * RecordSize) {
            static const char* fields[] = {"rank/byte1/accuracy",
                                           "enemies/failed",
                                           "clear_time",
                                           "damage",
                                           "plays",
                                           "first_set"};
            const qsizetype rel = offset - t.offset;
            return QStringLiteral("%1%2.%3")
                .arg(QLatin1String(t.name))
                .arg(rel / RecordSize)
                .arg(QLatin1String(fields[(rel % RecordSize) / 4]));
        }
    }
    return {};
}

bool inTables(qsizetype offset) {
    for (const Table& t : Tables) {
        if (offset >= t.offset && offset < t.offset + t.count * RecordSize) {
            return true;
        }
    }
    return false;
}

} // namespace

QJsonObject decodeUpload(const QByteArray& d) {
    QJsonObject out;
    if (!validUpload(d)) {
        out.insert("valid", false);
        out.insert("size", qint64(d.size()));
        return out;
    }
    out.insert("valid", true);
    out.insert("version", qint64(u32(d, 8)));
    out.insert("created", qint64(u32(d, 12)));

    QJsonObject totals;
    for (const Counter& c : Counters) {
        totals.insert(c.name, qint64(u32(d, c.offset)));
    }
    out.insert("totals", totals);

    QJsonArray characterRuns;
    for (int i = 0; i < CharacterCount; ++i) {
        const quint32 runs = u32(d, CharacterRunsOffset + i * 4);
        if (runs != 0) {
            characterRuns.append(QJsonObject{
                {"character", QLatin1String(Characters[i])}, {"index", i}, {"runs", qint64(runs)}});
        }
    }
    out.insert("character_runs", characterRuns);

    // Wanted enemies met: 255 entries of 0x28 bytes from 0x1DE0, each a running number (the
    // oldest is replaced when the list is full), the UTF-16 name shown on the enemy (16
    // characters), a u16 and the enemy kind as a subjugation ID.
    QJsonArray wanted;
    for (int i = 0; i < 255; ++i) {
        const qsizetype o = WantedOffset + i * WantedSize;
        const quint32 order = u32(d, o);
        if (order == 0) {
            continue;
        }
        QString name;
        for (int c = 0; c < 16; ++c) {
            const quint16 ch = u16(d, o + 4 + c * 2);
            if (ch == 0) {
                break;
            }
            name += QChar(ch);
        }
        wanted.append(QJsonObject{{"order", qint64(order)},
                                  {"name", name},
                                  {"value", qint64(u16(d, o + 0x24))},
                                  {"enemy_id", qint64(static_cast<quint8>(d[o + 0x26]))}});
    }
    out.insert("wanted", wanted);

    QJsonArray records;
    for (const Table& t : Tables) {
        for (int i = 0; i < t.count; ++i) {
            const qsizetype o = t.offset + i * RecordSize;
            const auto byte = [&](int n) { return qint64(static_cast<quint8>(d[o + n])); };
            const quint32 time = u32(d, o + 8);
            const quint32 damage = u32(d, o + 12);
            const bool neverCleared = byte(0) == 4 && byte(1) == 0 && byte(2) == 0 &&
                                      u32(d, o + 4) == 0 && time == 3600000 && damage == 100000 &&
                                      u32(d, o + 16) == 0 && u32(d, o + 20) == 0;
            if (neverCleared) {
                continue;
            }
            records.append(QJsonObject{
                {"table", QLatin1String(t.name)},
                {"index", i},
                {"name", stageRecordName(QLatin1String(t.name), i)},
                {"offset", qint64(o)},
                {"rank", byte(0)},
                {"unknown1", byte(1)},
                {"accuracy", byte(2)},
                {"enemies", qint64(u16(d, o + 4))},
                {"failed", qint64(u16(d, o + 6))},
                {"clear_time", qint64(time)},
                {"damage", qint64(damage)},
                {"plays", qint64(u32(d, o + 16))},
                {"first_set", qint64(u32(d, o + 20))},
            });
        }
    }
    out.insert("records", records);

    // Everything else that is not zero, to help map the rest.
    QJsonArray other;
    for (qsizetype o = 16; o + 4 <= d.size(); o += 4) {
        if (inTables(o) || inCharacterRuns(o) || o >= WantedOffset) {
            continue;
        }
        bool known = false;
        for (const Counter& c : Counters) {
            known = known || c.offset == o;
        }
        const quint32 v = u32(d, o);
        if (!known && v != 0) {
            other.append(QJsonObject{{"offset", qint64(o)}, {"value", qint64(v)}});
        }
    }
    out.insert("other", other);
    return out;
}

QJsonArray compareUploads(const QByteArray& before, const QByteArray& after) {
    QJsonArray out;
    if (before.size() != after.size()) {
        return out;
    }
    for (qsizetype o = 0; o + 4 <= after.size(); o += 4) {
        const quint32 a = u32(before, o);
        const quint32 b = u32(after, o);
        if (a != b) {
            out.append(QJsonObject{{"offset", qint64(o)},
                                   {"before", qint64(a)},
                                   {"after", qint64(b)},
                                   {"field", fieldName(o)}});
        }
    }
    return out;
}

QJsonArray personalBests(const QVector<QPair<QByteArray, qint64>>& uploads) {
    struct Best {
        QString table;
        int index = 0;
        qint64 rank = 99, rankAt = 0;
        qint64 time = -1, timeAt = 0;
        qint64 damage = -1, damageAt = 0;
        qint64 accuracy = -1, accuracyAt = 0;
        qint64 enemies = -1, enemiesAt = 0;
        qint64 plays = 0;
    };
    QVector<Best> bests;
    const auto find = [&](const QString& table, int index) -> Best& {
        for (Best& b : bests) {
            if (b.table == table && b.index == index) {
                return b;
            }
        }
        bests.append(Best{table, index});
        return bests.last();
    };
    for (const auto& [data, received] : uploads) {
        const QJsonArray records = decodeUpload(data).value("records").toArray();
        for (const QJsonValue& v : records) {
            const QJsonObject r = v.toObject();
            // A stage that was played but never cleared keeps the default time and damage.
            if (r.value("clear_time").toInteger() == 3600000) {
                continue;
            }
            Best& b = find(r.value("table").toString(), int(r.value("index").toInteger()));
            const qint64 rank = r.value("rank").toInteger();
            const qint64 time = r.value("clear_time").toInteger();
            const qint64 damage = r.value("damage").toInteger();
            const qint64 accuracy = r.value("accuracy").toInteger();
            const qint64 enemies = r.value("enemies").toInteger();
            // The record carries the latest clear; an unchanged record keeps its first time seen.
            if (rank < b.rank) {
                b.rank = rank;
                b.rankAt = received;
            }
            if (b.time < 0 || time < b.time) {
                b.time = time;
                b.timeAt = received;
            }
            if (b.damage < 0 || damage < b.damage) {
                b.damage = damage;
                b.damageAt = received;
            }
            if (accuracy > b.accuracy) {
                b.accuracy = accuracy;
                b.accuracyAt = received;
            }
            if (enemies > b.enemies) {
                b.enemies = enemies;
                b.enemiesAt = received;
            }
            b.plays = std::max(b.plays, r.value("plays").toInteger());
        }
    }
    QJsonArray out;
    for (const Best& b : bests) {
        out.append(QJsonObject{
            {"table", b.table},
            {"index", b.index},
            {"name", stageRecordName(b.table, b.index)},
            {"rank", b.rank},
            {"rank_at", b.rankAt},
            {"clear_time", b.time},
            {"clear_time_at", b.timeAt},
            {"damage", b.damage},
            {"damage_at", b.damageAt},
            {"accuracy", b.accuracy},
            {"accuracy_at", b.accuracyAt},
            {"enemies", b.enemies},
            {"enemies_at", b.enemiesAt},
            {"plays", b.plays},
        });
    }
    return out;
}
