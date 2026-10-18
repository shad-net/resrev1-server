// SPDX-FileCopyrightText: Copyright 2026 Resident Evil Revelations Custom Server Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "config.h"
#include "file_log.h"
#include "handler.h"
#include "http_server.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>

#include <algorithm>
#include <cstdio>

namespace {

Q_LOGGING_CATEGORY(lcRequest, "request")

constexpr qsizetype BodyPreviewBytes = 512;

int fatal(const QString& message) {
    std::fprintf(stderr, "error: %s\n", qPrintable(message));
    return 1;
}

QString preview(const QByteArray& body) {
    const QByteArray head = body.left(BodyPreviewBytes);
    const bool text = std::all_of(head.begin(), head.end(), [](char c) {
        const auto u = static_cast<unsigned char>(c);
        return u >= 0x20 || c == '\n' || c == '\r' || c == '\t';
    });
    QString out = text ? QString::fromUtf8(head) : QString::fromLatin1(head.toHex(' '));
    if (body.size() > head.size()) {
        out += QStringLiteral(" ... (%1 more bytes)").arg(body.size() - head.size());
    }
    return out;
}

void logRequest(const HttpRequest& request) {
    const QString query = request.query.isEmpty() ? QString() : '?' + request.query.toString();
    qCInfo(lcRequest).noquote() << QString::fromLatin1(request.method) + ' ' + request.path + query
                                << "from" << request.peer.toString();
    for (auto it = request.headers.constBegin(); it != request.headers.constEnd(); ++it) {
        qCInfo(lcRequest).noquote()
            << "  " + QString::fromLatin1(it.key()) + ":" << QString::fromLatin1(it.value());
    }
    if (!request.body.isEmpty()) {
        qCInfo(lcRequest).noquote()
            << "  body" << request.body.size() << "bytes:" << preview(request.body);
    }
}

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("game-server"));
    qSetMessagePattern(QStringLiteral("%{time yyyy-MM-dd hh:mm:ss} [%{type}] %{message}"));

    const QString configPath = QStringLiteral("config.json");
    if (!QFileInfo::exists(configPath)) {
        QFile file(configPath);
        if (!file.open(QIODevice::WriteOnly) || file.write(defaultConfigJson()) < 0) {
            return fatal(
                QStringLiteral("cannot create %1: %2").arg(configPath, file.errorString()));
        }
        qInfo("wrote default configuration to %s", qPrintable(configPath));
    }
    QFile file(configPath);
    if (!file.open(QIODevice::ReadOnly)) {
        return fatal(QStringLiteral("cannot read %1: %2").arg(configPath, file.errorString()));
    }
    QString error;
    auto config = loadConfig(file.readAll(), &error);
    if (!config) {
        return fatal(QStringLiteral("%1: %2").arg(configPath, error));
    }

    if (!config->logFile.isEmpty()) {
        const QString logPath =
            QDir(QFileInfo(configPath).absolutePath()).absoluteFilePath(config->logFile);
        if (!startFileLog(logPath, config->logMaxBytes, &error)) {
            return fatal(QStringLiteral("cannot open log file %1: %2").arg(logPath, error));
        }
        qInfo("logging to %s (rewritten at %lld MB)", qPrintable(logPath),
              static_cast<long long>(config->logMaxBytes / (1024 * 1024)));
    }

    const QString basePath = config->basePath;
    HttpServer server([basePath](const HttpRequest& request, HttpServer::Responder respond) {
        logRequest(request);
        QString path = request.path;
        if (!basePath.isEmpty()) {
            if (!path.startsWith(basePath + '/')) {
                qWarning("outside base path: %s", qPrintable(path));
                HttpResponse notFound;
                notFound.status = 404;
                notFound.contentType = "text/plain";
                notFound.body = "Not Found";
                respond(notFound);
                return;
            }
            path = path.mid(basePath.size());
        }
        handleRequest(request, path.mid(1), respond);
    });
    if (!server.listen(config->address, config->port, &error)) {
        return fatal(QStringLiteral("cannot listen on %1:%2: %3")
                         .arg(config->address.toString())
                         .arg(config->port)
                         .arg(error));
    }
    qInfo("listening on %s:%u, endpoints under %s", qPrintable(config->address.toString()),
          config->port, qPrintable(basePath.isEmpty() ? QStringLiteral("/") : basePath));
    return app.exec();
}
