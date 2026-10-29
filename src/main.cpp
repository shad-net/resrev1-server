// SPDX-FileCopyrightText: Copyright 2026 Resident Evil Revelations Custom Server Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "admin_service.h"
#include "config.h"
#include "file_log.h"
#include "http_server.h"
#include "revelations_service.h"
#include "storage.h"
#include "version.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QTimer>

#include <cstdio>

namespace {

std::optional<QByteArray> readFile(const QString& path, QString* error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = file.errorString();
        return std::nullopt;
    }
    return file.readAll();
}

bool writeFile(const QString& path, const QByteArray& data, QString* error) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(data) != data.size()) {
        *error = file.errorString();
        return false;
    }
    return true;
}

int fatal(const QString& message) {
    std::fprintf(stderr, "error: %s\n", qPrintable(message));
    return 1;
}

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("revelations-server"));
    QCoreApplication::setApplicationVersion(Revelations::Version());
    qSetMessagePattern(QStringLiteral("%{time yyyy-MM-dd hh:mm:ss} [%{type}] %{message}"));
    const QString versionLine = QStringLiteral("revelations-server %1 (built %2)")
                                    .arg(Revelations::Version(), Revelations::BuildTimestamp());
    qInfo("%s", qPrintable(versionLine));

    // config.json in the working directory, created with defaults on the first run.
    const QString configPath = QStringLiteral("config.json");
    QString error;
    if (!QFileInfo::exists(configPath)) {
        if (!writeFile(configPath, defaultConfigJson(), &error)) {
            return fatal(QStringLiteral("cannot create %1: %2").arg(configPath, error));
        }
        qInfo("wrote default configuration to %s", qPrintable(configPath));
    }
    const auto raw = readFile(configPath, &error);
    if (!raw) {
        return fatal(QStringLiteral("cannot read %1: %2").arg(configPath, error));
    }
    auto config = loadConfig(*raw, &error);
    if (!config) {
        return fatal(QStringLiteral("%1: %2").arg(configPath, error));
    }

    const QString configDir = QFileInfo(configPath).absolutePath();
    if (!config->logFile.isEmpty()) {
        const QString logPath = QDir(configDir).absoluteFilePath(config->logFile);
        if (!startFileLog(logPath, config->logMaxBytes, &error)) {
            return fatal(QStringLiteral("cannot open log file %1: %2").arg(logPath, error));
        }
        qInfo("logging to %s (rewritten at %lld MB)", qPrintable(logPath),
              static_cast<long long>(config->logMaxBytes / (1024 * 1024)));
        qInfo("%s", qPrintable(versionLine)); // the first line only reached the console
    }
    const QString dbPath = QDir(configDir).absoluteFilePath(config->database);
    Storage storage;
    if (!storage.open(dbPath, &error)) {
        return fatal(QStringLiteral("cannot open database %1: %2").arg(dbPath, error));
    }

    RevelationsService service(&storage, *config);
    AdminService admin(&storage, &service);
    if (!admin.loadStored(&error)) {
        return fatal(error);
    }
    HttpServer server([&service](const HttpRequest& request, HttpServer::Responder respond) {
        service.handle(request, std::move(respond));
    });
    if (!server.listen(config->address, config->port, &error)) {
        return fatal(QStringLiteral("cannot listen on %1:%2: %3")
                         .arg(config->address.toString())
                         .arg(config->port)
                         .arg(error));
    }
    qInfo("listening on %s:%u, endpoints under %s, %lld reward(s) configured, database %s",
          qPrintable(config->address.toString()), config->port, qPrintable(config->basePath),
          static_cast<long long>(config->rewards.size()), qPrintable(dbPath));
    HttpServer adminServer([&admin](const HttpRequest& request, HttpServer::Responder respond) {
        admin.handle(request, std::move(respond));
    });
    if (config->adminKey.isEmpty()) {
        qInfo("admin API disabled: set admin.key in the config to use revelations-admin");
    } else if (!adminServer.listen(config->adminAddress, config->adminPort, &error)) {
        return fatal(QStringLiteral("cannot listen on %1:%2 for the admin API: %3")
                         .arg(config->adminAddress.toString())
                         .arg(config->adminPort)
                         .arg(error));
    } else {
        qInfo("admin API on %s:%u, %lld reward(s) added through it",
              qPrintable(config->adminAddress.toString()), config->adminPort,
              static_cast<long long>(service.storedRewards().size()));
    }
    qInfo("verifying authorization codes with shadNet at %s", qPrintable(config->shadnetUrl));

    QFileSystemWatcher watcher({QFileInfo(configPath).absoluteFilePath()});
    QTimer reloadDelay;
    reloadDelay.setSingleShot(true);
    reloadDelay.setInterval(300);
    QObject::connect(&watcher, &QFileSystemWatcher::fileChanged, &reloadDelay,
                     qOverload<>(&QTimer::start));
    QObject::connect(&reloadDelay, &QTimer::timeout, [&] {
        const QString absolute = QFileInfo(configPath).absoluteFilePath();
        if (!watcher.files().contains(absolute) && QFileInfo::exists(absolute)) {
            watcher.addPath(absolute);
        }
        QString reloadError;
        const auto data = readFile(configPath, &reloadError);
        const auto reloaded = data ? loadConfig(*data, &reloadError) : std::nullopt;
        if (!reloaded) {
            qWarning("config not reloaded, keeping the previous one: %s", qPrintable(reloadError));
            return;
        }
        ServerConfig next = *reloaded;
        next.address = config->address;
        next.port = config->port;
        next.database = config->database;
        if (next.logFile != config->logFile || next.logMaxBytes != config->logMaxBytes) {
            qWarning("log settings changed: they apply after a restart");
        }
        next.adminAddress = config->adminAddress;
        next.adminPort = config->adminPort;
        if (next.adminKey.isEmpty() != config->adminKey.isEmpty()) {
            qWarning("enabling or disabling the admin API needs a restart");
            next.adminKey = config->adminKey;
        }
        service.setConfig(next);
        qInfo("config reloaded: %lld reward(s)", static_cast<long long>(next.rewards.size()));
    });

    return app.exec();
}
