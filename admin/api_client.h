// SPDX-FileCopyrightText: Copyright 2026 Resident Evil Revelations Custom Server Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QByteArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QUrl>

#include <functional>

// Talks to revelations-server's admin API.
class ApiClient : public QObject {
    Q_OBJECT
public:
    struct Reply {
        int status = 0; // HTTP status, 0 when the server could not be reached
        QByteArray body;
        QString error; // set when the request failed
        QJsonDocument json() const {
            return QJsonDocument::fromJson(body);
        }
        bool ok() const {
            return error.isEmpty();
        }
    };
    using Callback = std::function<void(const Reply&)>;

    explicit ApiClient(QObject* parent = nullptr);

    void setServer(const QUrl& base, const QString& key);
    bool configured() const {
        return base_.isValid() && !key_.isEmpty();
    }

    void get(const QString& path, Callback done);
    void post(const QString& path, const QJsonDocument& body, Callback done);
    void put(const QString& path, const QJsonDocument& body, Callback done);
    void remove(const QString& path, Callback done);

private:
    void send(const QByteArray& verb, const QString& path, const QByteArray& body, Callback done);

    QNetworkAccessManager net_;
    QUrl base_;
    QString key_;
};
