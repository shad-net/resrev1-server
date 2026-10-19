// SPDX-FileCopyrightText: Copyright 2026 Resident Evil Revelations Custom Server Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "file_log.h"

#include <QFile>
#include <QMutex>
#include <QMutexLocker>

#include <cstdio>

namespace {

struct FileLog {
    QMutex mutex;
    QFile file;
    qint64 maxBytes = 0;
    QtMessageHandler previous = nullptr;
};

FileLog& state() {
    static FileLog log;
    return log;
}

void restart(FileLog& log) {
    log.file.close();
    if (!log.file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        std::fprintf(stderr, "cannot reopen log file %s\n", qPrintable(log.file.fileName()));
    }
}

void handler(QtMsgType type, const QMessageLogContext& context, const QString& message) {
    FileLog& log = state();
    if (log.previous) {
        log.previous(type, context, message);
    }
    const QByteArray line =
        (qFormatLogMessage(type, context, message) + QLatin1Char('\n')).toUtf8();
    QMutexLocker lock(&log.mutex);
    if (!log.file.isOpen()) {
        return;
    }
    if (log.file.size() + line.size() > log.maxBytes) {
        restart(log);
        if (!log.file.isOpen()) {
            return;
        }
    }
    log.file.write(line);
    log.file.flush();
}

} // namespace

bool startFileLog(const QString& path, qint64 maxBytes, QString* error) {
    FileLog& log = state();
    QMutexLocker lock(&log.mutex);
    log.maxBytes = maxBytes;
    log.file.setFileName(path);
    if (!log.file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        *error = log.file.errorString();
        return false;
    }
    log.previous = qInstallMessageHandler(handler);
    return true;
}
