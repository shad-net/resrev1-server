// SPDX-FileCopyrightText: Copyright 2026 Resident Evil Revelations Custom Server Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QDialog>
#include <QJsonObject>

class EventEditor;
class QComboBox;
class QLabel;
class QLineEdit;
class WindowEditor;

class ScheduleDialog : public QDialog {
    Q_OBJECT
public:
    explicit ScheduleDialog(const QJsonObject& entry, QWidget* parent = nullptr);
    QJsonObject entry() const {
        return entry_;
    }

private:
    void accept() override;

    QLineEdit* name_;
    QLineEdit* description_;
    WindowEditor* window_;
    QComboBox* preset_;
    EventEditor* event_;
    QLabel* error_;
    QJsonObject entry_;
};
