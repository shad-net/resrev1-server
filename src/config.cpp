// SPDX-FileCopyrightText: Copyright 2026 shadNet Custom Server Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "config.h"

#include <QJsonDocument>
#include <QJsonObject>

namespace {

bool readInt(const QJsonValue& v, qint64 min, qint64 max, qint64* out) {
    if (!v.isDouble()) {
        return false;
    }
    const double d = v.toDouble();
    const qint64 i = static_cast<qint64>(d);
    if (d != static_cast<double>(i) || i < min || i > max) {
        return false;
    }
    *out = i;
    return true;
}

std::optional<Config> failed(QString* error, const QString& message) {
    *error = message;
    return std::nullopt;
}

} // namespace

std::optional<Config> loadConfig(const QByteArray& json, QString* error) {
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        return failed(
            error, QStringLiteral("config is not a JSON object: %1").arg(parseError.errorString()));
    }
    const QJsonObject root = doc.object();
    Config c;

    const QJsonObject listen = root.value("listen").toObject();
    if (listen.contains("address") && !c.address.setAddress(listen.value("address").toString())) {
        return failed(error, QStringLiteral("listen.address is not an IP address"));
    }
    if (listen.contains("port")) {
        qint64 port = 0;
        if (!readInt(listen.value("port"), 1, 65535, &port)) {
            return failed(error, QStringLiteral("listen.port must be 1..65535"));
        }
        c.port = static_cast<quint16>(port);
    }

    c.basePath = root.value("base_path").toString();
    while (c.basePath.endsWith('/')) {
        c.basePath.chop(1);
    }
    if (!c.basePath.isEmpty() && !c.basePath.startsWith('/')) {
        return failed(error, QStringLiteral("base_path must start with /"));
    }

    const QJsonObject log = root.value("log").toObject();
    c.logFile = log.value("file").toString();
    if (log.contains("max_size_mb")) {
        qint64 mb = 0;
        if (!readInt(log.value("max_size_mb"), 1, 4096, &mb)) {
            return failed(error, QStringLiteral("log.max_size_mb must be 1..4096"));
        }
        c.logMaxBytes = mb * 1024 * 1024;
    }
    return c;
}

QByteArray defaultConfigJson() {
    return QByteArrayLiteral(R"({
    "listen": { "address": "0.0.0.0", "port": 8080 },
    "base_path": "",
    "log": { "file": "server.log", "max_size_mb": 10 }
}
)");
}
