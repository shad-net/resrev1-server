// SPDX-FileCopyrightText: Copyright 2026 Resident Evil Revelations Custom Server Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "traffic.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QStringList>
#include <QtEndian>

#include <algorithm>

namespace {

Q_LOGGING_CATEGORY(lcTrace, "trace")

constexpr qsizetype KeptBodyChars = 8 * 1024;
constexpr qsizetype KeptHexBytes = 256;

struct Store {
    QList<TrafficEntry> entries;
    quint64 next = 1;
};

Store& store() {
    static Store s;
    return s;
}

QByteArray stripNul(QByteArray data) {
    while (data.endsWith('\0')) {
        data.chop(1);
    }
    return data;
}

bool printable(const QByteArray& data) {
    return std::all_of(data.begin(), data.end(), [](char c) {
        const auto u = static_cast<unsigned char>(c);
        return u >= 0x20 || c == '\n' || c == '\r' || c == '\t';
    });
}

QString hexDump(const QByteArray& data) {
    QStringList lines;
    for (qsizetype off = 0; off < data.size(); off += 16) {
        const QByteArray row = data.mid(off, 16);
        QString ascii;
        for (char c : row) {
            const auto u = static_cast<unsigned char>(c);
            ascii += (u >= 0x20 && u < 0x7f) ? QChar::fromLatin1(c) : QLatin1Char('.');
        }
        lines << QStringLiteral("%1  %2  %3")
                     .arg(off, 5, 16, QLatin1Char('0'))
                     .arg(QString::fromLatin1(row.toHex(' ')).leftJustified(47), ascii);
    }
    return lines.join('\n');
}

QString cut(const QString& text, qsizetype limit) {
    if (limit <= 0 || text.size() <= limit) {
        return text;
    }
    return text.left(limit) + QStringLiteral("\n... (%1 more characters)").arg(text.size() - limit);
}

} // namespace

namespace Traffic {

quint64 nextId() {
    return store().next++;
}

void record(const TrafficEntry& entry) {
    auto& entries = store().entries;
    entries.append(entry);
    if (entries.size() > MaxEntries) {
        entries.remove(0, entries.size() - MaxEntries);
    }
}

QList<TrafficEntry> since(quint64 after) {
    QList<TrafficEntry> out;
    for (const TrafficEntry& e : store().entries) {
        if (e.id > after) {
            out.append(e);
        }
    }
    return out;
}

std::optional<TrafficEntry> find(quint64 id) {
    for (const TrafficEntry& e : store().entries) {
        if (e.id == id) {
            return e;
        }
    }
    return std::nullopt;
}

quint64 lastId() {
    return store().next - 1;
}

QString describe(const QByteArray& raw, qsizetype textLimit, qsizetype hexLimit) {
    if (raw.isEmpty()) {
        return {};
    }
    const QByteArray plain = stripNul(raw);
    const QByteArray trimmed = plain.trimmed();
    if (trimmed.startsWith('{') || trimmed.startsWith('[')) {
        QJsonParseError error{};
        const QJsonDocument doc = QJsonDocument::fromJson(trimmed, &error);
        if (error.error == QJsonParseError::NoError) {
            return cut(QString::fromUtf8(doc.toJson(QJsonDocument::Compact)), textLimit);
        }
    }
    if (printable(plain.left(512))) {
        return cut(QString::fromUtf8(plain), textLimit);
    }
    QString summary = QStringLiteral("%1 bytes of binary data").arg(raw.size());
    if (raw.startsWith("REBH") && raw.size() >= 12) {
        const auto version = qFromLittleEndian<quint32>(raw.constData() + 8);
        summary =
            QStringLiteral("%1-byte player upload (REBH, version %2)").arg(raw.size()).arg(version);
    }
    const qsizetype shown = hexLimit > 0 ? std::min(hexLimit, raw.size()) : raw.size();
    QString out = summary + '\n' + hexDump(raw.left(shown));
    if (shown < raw.size()) {
        out += QStringLiteral("\n... (%1 more bytes)").arg(raw.size() - shown);
    }
    return out;
}

QString keep(const QByteArray& plain) {
    return describe(plain, KeptBodyChars, KeptHexBytes);
}

QString resultOf(const QByteArray& plain) {
    const QJsonDocument doc = QJsonDocument::fromJson(stripNul(plain));
    return doc.isObject() ? doc.object().value(QStringLiteral("result")).toString() : QString();
}

void trace(const TrafficEntry& e, const QByteArray& request, const QByteArray& response,
           qsizetype limit) {
    const QString who =
        e.player.isEmpty() ? e.peer : e.player + QStringLiteral(" (") + e.peer + ')';
    const QString line =
        QStringLiteral("#%1 %2 %3 from %4 -> %5%6, %7 ms, %8 bytes in, %9 bytes out")
            .arg(e.id)
            .arg(e.method, e.path, who)
            .arg(e.status)
            .arg(e.result.isEmpty() ? QString() : ' ' + e.result)
            .arg(e.elapsedMs)
            .arg(e.bytesIn)
            .arg(e.bytesOut);
    if (e.status >= 400 || e.result == QLatin1String("NG")) {
        qCWarning(lcTrace).noquote() << line;
    } else {
        qCInfo(lcTrace).noquote() << line;
    }
    const auto block = [&e](const QString& label, const QString& text) {
        if (text.isEmpty()) {
            return;
        }
        for (const QString& part : text.split('\n')) {
            qCInfo(lcTrace).noquote() << QStringLiteral("#%1   %2 %3").arg(e.id).arg(label, part);
        }
    };
    static QString lastHeaders;
    QStringList stable;
    for (const QString& h : e.headers.split('\n')) {
        if (!h.startsWith(QLatin1String("content-length:")) &&
            !h.startsWith(QLatin1String("x-np-authcode:"))) {
            stable << h;
        }
    }
    if (stable.join('\n') != lastHeaders) {
        lastHeaders = stable.join('\n');
        block(QStringLiteral("header  "), e.headers);
    }
    block(QStringLiteral("request "),
          request.isEmpty() ? QString() : describe(request, limit, limit));
    block(QStringLiteral("response"),
          response.isEmpty() ? QString() : describe(response, limit, limit));
}

} // namespace Traffic
