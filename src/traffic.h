// SPDX-FileCopyrightText: Copyright 2026 Resident Evil Revelations Custom Server Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

#include <optional>

struct TrafficEntry {
    quint64 id = 0;
    qint64 time = 0; // ms since epoch
    QString method;
    QString path;     // as requested, with the query
    QString endpoint; // "regev.php", "leaderboards", or the last path part for unknown requests
    QString peer;
    QString player;  // shadNet online ID, empty when the request never got that far
    QString headers; // "name: value" lines, the auth code shortened
    int status = 0;
    QString result; // "OK" or "NG" from the answer's result field, empty when it has none
    qint64 elapsedMs = 0;
    qint64 bytesIn = 0;
    qint64 bytesOut = 0;
    QString request;
    QString response;
};

namespace Traffic {

constexpr qsizetype MaxEntries = 1000;

quint64 nextId();
void record(const TrafficEntry& entry);
QList<TrafficEntry> since(quint64 after);
std::optional<TrafficEntry> find(quint64 id);
quint64 lastId();
QString describe(const QByteArray& plain, qsizetype textLimit, qsizetype hexLimit);
QString keep(const QByteArray& plain);
QString resultOf(const QByteArray& plain);
void trace(const TrafficEntry& entry, const QByteArray& request, const QByteArray& response,
           qsizetype limit);

} // namespace Traffic
