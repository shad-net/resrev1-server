// SPDX-FileCopyrightText: Copyright 2026 shadNet Custom Server Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QByteArray>
#include <QHostAddress>
#include <QString>

#include <optional>

struct Config {
    QHostAddress address = QHostAddress::Any;
    quint16 port = 8080;
    // Prefix in front of every endpoint, e.g. "/game/v1"; empty = none.
    QString basePath;
    // Log file ,empty = console only. Rewritten from the start
    // when it reaches logMaxBytes.
    QString logFile;
    qint64 logMaxBytes = 10 * 1024 * 1024;
};

std::optional<Config> loadConfig(const QByteArray& json, QString* error);
QByteArray defaultConfigJson();
