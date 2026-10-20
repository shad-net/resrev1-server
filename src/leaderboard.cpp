// SPDX-FileCopyrightText: Copyright 2026 Resident Evil Revelations Custom Server Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "leaderboard.h"

#include "storage.h"
#include "upload_decoder.h"

#include <QDateTime>
#include <QJsonArray>
#include <QMap>
#include <QVector>

#include <algorithm>

namespace {

struct StageEntry {
    QString player;
    qint64 time = 0;
    qint64 rank = 0;
    qint64 damage = 0;
    qint64 accuracy = 0;
    qint64 enemies = 0;
    qint64 set = 0;
};

struct PlayerEntry {
    QString player;
    qint64 level = 0;
    qint64 enemies = 0;
    qint64 timePlayed = 0;
    qint64 runs = 0;
    qint64 cleared = 0;
};

QString duration(qint64 seconds) {
    if (seconds < 3600) {
        return QStringLiteral("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QLatin1Char('0'));
    }
    return QStringLiteral("%1:%2:%3")
        .arg(seconds / 3600)
        .arg((seconds / 60) % 60, 2, 10, QLatin1Char('0'))
        .arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

QString rankName(qint64 rank) {
    // 0 = S and 1 = A are confirmed in game, 2 and 3 follow the order (B, C).
    switch (rank) {
    case 0:
        return QStringLiteral("S");
    case 1:
        return QStringLiteral("A");
    case 2:
        return QStringLiteral("B");
    case 3:
        return QStringLiteral("C");
    case 4:
        return QStringLiteral("-");
    default:
        return QString::number(rank);
    }
}

QString escape(const QString& text) {
    return text.toHtmlEscaped();
}

} // namespace

QString stageRecordName(const QString& table, int index) {
    static const char* stages[] = {"Crew Quarters Area 1",
                                   "Beach",
                                   "Crew Quarters Area 2",
                                   "Snowy Mountain 1",
                                   "Guest Cabin",
                                   "Promenade Deck",
                                   "Terragrigia 1",
                                   "Crew Quarters Area 3",
                                   "Bilge Block",
                                   "Snowy Mountain 2",
                                   "Semiramis",
                                   "Observation Deck",
                                   "Side Deck",
                                   "Flood",
                                   "Zenobia",
                                   "Airstrip",
                                   "Laboratory",
                                   "Emergency Passageway",
                                   "Terragrigia 2",
                                   "Mad Banquet"};
    static const char* difficulties[] = {"Chasm", "Trench", "Abyss"};
    if (table == QLatin1String("B") && index >= 0 && index < 60) {
        return QStringLiteral("Stage %1: %2, %3")
            .arg(index % 20 + 1)
            .arg(QLatin1String(stages[index % 20]), QLatin1String(difficulties[index / 20]));
    }
    if (table == QLatin1String("B") && (index == 60 || index == 61)) {
        return QStringLiteral("Bonus Stage: The Ghost Ship, %1")
            .arg(QLatin1String(difficulties[index - 60]));
    }
    // Episode and chapter of each Campaign record
    static const int chapters[26][2] = {{1, 1},  {1, 2},  {1, 3},  {2, 1},  {2, 2}, {3, 1}, {3, 2},
                                        {4, 1},  {5, 2},  {5, 1},  {5, 3},  {6, 1}, {6, 2}, {7, 1},
                                        {7, 2},  {8, 1},  {8, 2},  {8, 3},  {9, 1}, {9, 2}, {10, 1},
                                        {10, 2}, {11, 1}, {11, 2}, {12, 2}, {12, 3}};
    if (table == QLatin1String("A") && index >= 0 && index < 78) {
        const int c = index % 26;
        // The game's own difficulty order
        static const char* campaign[] = {"Casual", "Normal", "Infernal"};
        return QStringLiteral("Campaign Episode %1-%2, %3")
            .arg(chapters[c][0])
            .arg(chapters[c][1])
            .arg(QLatin1String(campaign[index / 26]));
    }
    return QStringLiteral("Record %1%2").arg(table).arg(index);
}

QJsonObject buildLeaderboards(Storage& storage, int size) {
    const auto players = storage.players();
    if (!players) {
        return QJsonObject{{"error", QStringLiteral("storage error")}};
    }

    QMap<QPair<QString, int>, QVector<StageEntry>> stages;
    QVector<PlayerEntry> overall;
    for (const PlayerInfo& p : *players) {
        if (p.uploadUpdated == 0) {
            continue;
        }
        const auto history = storage.history(p.id, true);
        if (!history || history->isEmpty()) {
            continue;
        }
        const QString name = p.onlineId.isEmpty() ? p.code : p.onlineId;
        QVector<QPair<QByteArray, qint64>> uploads;
        for (const HistoryUpload& h : *history) {
            uploads.append({h.data, h.received});
        }

        const QJsonArray bests = personalBests(uploads);
        for (const QJsonValue& v : bests) {
            const QJsonObject b = v.toObject();
            stages[{b.value("table").toString(), int(b.value("index").toInteger())}].append(
                StageEntry{
                    name,
                    b.value("clear_time").toInteger(),
                    b.value("rank").toInteger(),
                    b.value("damage").toInteger(),
                    b.value("accuracy").toInteger(),
                    b.value("enemies").toInteger(),
                    b.value("clear_time_at").toInteger(),
                });
        }

        const QJsonObject last = decodeUpload(history->last().data);
        if (!last.value("valid").toBool()) {
            continue;
        }
        const QJsonObject totals = last.value("totals").toObject();
        overall.append(PlayerEntry{
            name,
            totals.value("level").toInteger(),
            totals.value("enemies_total").toInteger(),
            totals.value("clear_time_total").toInteger(),
            totals.value("runs_total").toInteger(),
            qint64(bests.size()),
        });
    }

    QJsonArray stageBoards;
    for (auto it = stages.begin(); it != stages.end(); ++it) {
        QVector<StageEntry>& entries = it.value();
        std::sort(entries.begin(), entries.end(), [](const StageEntry& a, const StageEntry& b) {
            if (a.time != b.time) {
                return a.time < b.time;
            }
            if (a.rank != b.rank) {
                return a.rank < b.rank;
            }
            if (a.damage != b.damage) {
                return a.damage < b.damage;
            }
            return a.set < b.set; // the earlier record wins a full tie
        });
        QJsonArray rows;
        for (int i = 0; i < entries.size() && (size <= 0 || i < size); ++i) {
            const StageEntry& e = entries[i];
            rows.append(QJsonObject{
                {"place", i + 1},
                {"player", e.player},
                {"clear_time", e.time},
                {"rank", e.rank},
                {"damage", e.damage},
                {"accuracy", e.accuracy},
                {"enemies", e.enemies},
                {"set", e.set},
            });
        }
        stageBoards.append(QJsonObject{
            {"table", it.key().first},
            {"index", it.key().second},
            {"name", stageRecordName(it.key().first, it.key().second)},
            {"entries", rows},
        });
    }

    std::sort(overall.begin(), overall.end(), [](const PlayerEntry& a, const PlayerEntry& b) {
        if (a.level != b.level) {
            return a.level > b.level;
        }
        if (a.enemies != b.enemies) {
            return a.enemies > b.enemies;
        }
        return a.player.compare(b.player, Qt::CaseInsensitive) < 0;
    });
    QJsonArray playerRows;
    for (int i = 0; i < overall.size() && (size <= 0 || i < size); ++i) {
        const PlayerEntry& e = overall[i];
        playerRows.append(QJsonObject{
            {"place", i + 1},
            {"player", e.player},
            {"level", e.level},
            {"enemies", e.enemies},
            {"time_played", e.timePlayed},
            {"runs", e.runs},
            {"stages_cleared", e.cleared},
        });
    }

    return QJsonObject{
        {"generated", QDateTime::currentSecsSinceEpoch()},
        {"stages", stageBoards},
        {"players", playerRows},
    };
}

QByteArray leaderboardsHtml(const QJsonObject& boards) {
    QString html = QStringLiteral(R"(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>Raid Mode Leaderboards</title>
<style>
:root { --bg: #0f1418; --panel: #172026; --text: #dfe8ec; --muted: #8fa3ad; --line: #26343c; --accent: #7fc4d8; }
* { box-sizing: border-box; }
body { margin: 0; background: var(--bg); color: var(--text); font: 15px/1.45 system-ui, sans-serif; }
main { max-width: 900px; margin: 0 auto; padding: 24px 16px 48px; }
h1 { font-size: 26px; margin: 0 0 4px; letter-spacing: .02em; }
h2 { font-size: 18px; margin: 32px 0 8px; color: var(--accent); }
p.note { color: var(--muted); margin: 0 0 8px; }
.wrap { overflow-x: auto; background: var(--panel); border: 1px solid var(--line); border-radius: 8px; }
table { border-collapse: collapse; width: 100%; min-width: 520px; }
th, td { padding: 8px 12px; text-align: right; border-bottom: 1px solid var(--line); white-space: nowrap; }
th { color: var(--muted); font-weight: 600; font-size: 13px; }
th:nth-child(2), td:nth-child(2) { text-align: left; }
tr:last-child td { border-bottom: 0; }
td.place { color: var(--muted); width: 3em; }
tr.first td { color: #f2d27a; }
.empty { padding: 16px; color: var(--muted); }
.event { background: var(--panel); border: 1px solid var(--line); border-left: 3px solid var(--accent); border-radius: 8px; padding: 12px 16px; }
.event p { margin: 4px 0; }
.event .title { font-weight: 600; font-size: 16px; }
.event .title.small { font-size: 15px; margin-top: 10px; }
.event .title span { color: var(--muted); font-weight: 400; font-size: 14px; }
.event .none { color: var(--muted); }
.event .next { color: var(--muted); font-size: 13px; text-transform: uppercase; letter-spacing: .06em; margin-top: 14px; }
.event ul { margin: 4px 0 4px; padding-left: 20px; }
</style></head><body><main>
<h1>Raid Mode Leaderboards</h1>
<p class="note">Built from the players' uploads after each Raid stage. Updated %1.</p>
)")
                       .arg(QDateTime::fromSecsSinceEpoch(boards.value("generated").toInteger())
                                .toUTC()
                                .toString(QStringLiteral("yyyy-MM-dd HH:mm 'UTC'")));

    // Current and upcoming events: the game itself never shows them.
    const QJsonObject event = boards.value("event").toObject();
    const QJsonObject current = event.value("current").toObject();
    // Shown in UTC, and in the viewer's own time zone once the script below runs.
    const auto when = [](qint64 secs) {
        return QStringLiteral("<time data-t=\"%1\">%2</time>")
            .arg(secs)
            .arg(QDateTime::fromSecsSinceEpoch(secs).toUTC().toString(
                QStringLiteral("ddd d MMM, HH:mm 'UTC'")));
    };
    const auto effects = [](const QJsonObject& o) {
        QString out;
        if (!o.value("text").toString().isEmpty()) {
            out += QStringLiteral("<p>%1</p>").arg(escape(o.value("text").toString()));
        }
        const QJsonArray lines = o.value("description").toArray();
        if (!lines.isEmpty()) {
            out += QStringLiteral("<ul>");
            for (const QJsonValue& l : lines) {
                out += QStringLiteral("<li>%1</li>").arg(escape(l.toString()));
            }
            out += QStringLiteral("</ul>");
        }
        return out;
    };
    html += QStringLiteral("<h2>Event</h2><div class=\"event\">");
    if (current.value("description").toArray().isEmpty() &&
        current.value("text").toString().isEmpty()) {
        html += QStringLiteral("<p class=\"none\">No event is running.</p>");
    } else {
        html += QStringLiteral("<p class=\"title\">%1%2</p>")
                    .arg(escape(current.value("name").toString(QStringLiteral("Running now"))))
                    .arg(current.contains("end") ? QStringLiteral(" <span>until %1</span>")
                                                       .arg(when(current.value("end").toInteger()))
                                                 : QString());
        html += effects(current);
    }
    const QJsonArray upcoming = event.value("upcoming").toArray();
    if (!upcoming.isEmpty()) {
        html += QStringLiteral("<p class=\"next\">Coming up</p>");
        for (const QJsonValue& v : upcoming) {
            const QJsonObject o = v.toObject();
            html += QStringLiteral("<p class=\"title small\">%1 <span>%2 to %3</span></p>")
                        .arg(escape(o.value("name").toString()), when(o.value("start").toInteger()),
                             when(o.value("end").toInteger()));
            html += effects(o);
        }
    }
    html += QStringLiteral("</div>");

    html += QStringLiteral("<h2>Players</h2><div class=\"wrap\">");
    const QJsonArray players = boards.value("players").toArray();
    if (players.isEmpty()) {
        html += QStringLiteral("<div class=\"empty\">No uploads yet.</div>");
    } else {
        html += QStringLiteral(
            "<table><tr><th>#</th><th>Player</th><th>Level</th><th>Enemies routed</th>"
            "<th>Time played</th><th>Raid runs</th><th>Stages cleared</th></tr>");
        for (const QJsonValue& v : players) {
            const QJsonObject r = v.toObject();
            html +=
                QStringLiteral(
                    "<tr%1><td class=\"place\">%2</td><td>%3</td><td>%4</td><td>%5</td><td>%6</td>"
                    "<td>%7</td><td>%8</td></tr>")
                    .arg(r.value("place").toInteger() == 1 ? QStringLiteral(" class=\"first\"")
                                                           : QString())
                    .arg(r.value("place").toInteger())
                    .arg(escape(r.value("player").toString()))
                    .arg(r.value("level").toInteger())
                    .arg(r.value("enemies").toInteger())
                    .arg(duration(r.value("time_played").toInteger()))
                    .arg(r.value("runs").toInteger())
                    .arg(r.value("stages_cleared").toInteger());
        }
        html += QStringLiteral("</table>");
    }
    html += QStringLiteral("</div>");

    for (const QJsonValue& s : boards.value("stages").toArray()) {
        const QJsonObject stage = s.toObject();
        html +=
            QStringLiteral("<h2>%1</h2><div class=\"wrap\"><table><tr><th>#</th><th>Player</th>"
                           "<th>Clear time</th><th>Rank</th><th>Damage taken</th><th>Accuracy</th>"
                           "<th>Enemies</th></tr>")
                .arg(escape(stage.value("name").toString()));
        for (const QJsonValue& v : stage.value("entries").toArray()) {
            const QJsonObject r = v.toObject();
            html +=
                QStringLiteral(
                    "<tr%1><td class=\"place\">%2</td><td>%3</td><td>%4</td><td>%5</td><td>%6</td>"
                    "<td>%7 %</td><td>%8</td></tr>")
                    .arg(r.value("place").toInteger() == 1 ? QStringLiteral(" class=\"first\"")
                                                           : QString())
                    .arg(r.value("place").toInteger())
                    .arg(escape(r.value("player").toString()))
                    .arg(duration(r.value("clear_time").toInteger()))
                    .arg(rankName(r.value("rank").toInteger()))
                    .arg(r.value("damage").toInteger())
                    .arg(r.value("accuracy").toInteger())
                    .arg(r.value("enemies").toInteger());
        }
        html += QStringLiteral("</table></div>");
    }
    if (boards.value("stages").toArray().isEmpty()) {
        html += QStringLiteral(
            "<h2>Stages</h2><div class=\"wrap\"><div class=\"empty\">No stage cleared yet."
            "</div></div>");
    }
    html += QStringLiteral(
        "<p class=\"note\" style=\"margin-top:24px\">Stage boards rank each player's fastest "
        "clear, "
        "rank, damage and accuracy are that player's bests on the stage.</p>"
        "</main><script>"
        "for (const t of document.querySelectorAll('time[data-t]')) {"
        " try { t.textContent = new Date(t.dataset.t * 1000).toLocaleString(undefined,"
        " {weekday: 'short', day: 'numeric', month: 'short', hour: '2-digit', minute: '2-digit'}); "
        "}"
        " catch (e) {} }"
        "</script></body></html>\n");
    return html.toUtf8();
}
