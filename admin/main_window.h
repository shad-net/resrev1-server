// SPDX-FileCopyrightText: Copyright 2026 Resident Evil Revelations Custom Server Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "api_client.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QMainWindow>

class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class QTabWidget;
class EventEditor;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

    // Fills the connection fields and connects (used by the command line).
    void connectTo(const QString& url, const QString& key);

private:
    QWidget* buildPlayersTab();
    QWidget* buildRewardsTab();
    QWidget* buildEventTab();
    void displayEvent(const QJsonObject& reply);
    void applyEvent();
    void clearEvent();

    void connectToServer();
    void refresh();
    void showPlayers(const QJsonArray& players);
    void showRewards(const QJsonArray& rewards);
    void updateButtons();
    void setStatus(const QString& text, bool error = false);
    void reportError(const QString& action, const ApiClient::Reply& reply);

    QJsonObject selectedPlayer() const;
    QJsonObject selectedReward() const;
    qint64 nextSerial() const;

    void addReward(const QStringList& players);
    void deleteReward();
    void resetClaims();
    void saveUpload();
    void showStats();

    ApiClient api_;
    QJsonArray players_;
    QJsonArray rewards_;

    QLineEdit* url_;
    QLineEdit* key_;
    QPushButton* connect_;
    QLabel* server_;
    QTabWidget* tabs_;
    QTableWidget* playerTable_;
    QTableWidget* rewardTable_;
    QPushButton* giveReward_;
    QPushButton* resetClaims_;
    QPushButton* saveUpload_;
    QPushButton* stats_;
    QPushButton* deleteReward_;
    QPushButton* addReward_;
    QPushButton* refresh_;
    EventEditor* eventEditor_;
    QLabel* eventSource_;
    QPushButton* applyEvent_;
    QPushButton* clearEvent_;
    QPushButton* revertEvent_;
    QJsonObject eventReply_;
};
