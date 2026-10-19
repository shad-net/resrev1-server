// SPDX-FileCopyrightText: Copyright 2026 Resident Evil Revelations Custom Server Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QDialog>
#include <QJsonObject>

class ApiClient;
class QComboBox;
class QFormLayout;
class QLabel;
class QTabWidget;
class QTableWidget;

class StatsDialog : public QDialog {
    Q_OBJECT
public:
    StatsDialog(ApiClient* api, qint64 playerId, const QString& player, QWidget* parent = nullptr);

private:
    void loadHistory();
    void loadUpload(qint64 uploadId);
    void display(const QJsonObject& stats);

    ApiClient* api_;
    qint64 playerId_;
    QComboBox* uploads_;
    QLabel* status_;
    QLabel* level_;
    QLabel* enemies_;
    QLabel* clearTime_;
    QLabel* stagesCleared_;
    QLabel* created_;
    QTabWidget* tabs_;
    QTableWidget* records_;
    QTableWidget* bests_;
    QTableWidget* changes_;
    QTableWidget* other_;
};
