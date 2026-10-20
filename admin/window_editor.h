// SPDX-FileCopyrightText: Copyright 2026 Resident Evil Revelations Custom Server Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QJsonObject>
#include <QWidget>

class QComboBox;
class QDateTimeEdit;
class QRadioButton;
class QStackedWidget;
class QTimeEdit;

class WindowEditor : public QWidget {
    Q_OBJECT
public:
    explicit WindowEditor(bool allowAlways, QWidget* parent = nullptr);
    void setWindow(const QJsonObject& o);
    QJsonObject window() const;

signals:
    void changed();

private:
    QRadioButton* always_ = nullptr;
    QRadioButton* once_;
    QRadioButton* weekly_;
    QStackedWidget* pages_;
    QComboBox* zone_;
    QDateTimeEdit* onceStart_;
    QDateTimeEdit* onceEnd_;
    QComboBox* startDay_;
    QTimeEdit* startTime_;
    QComboBox* endDay_;
    QTimeEdit* endTime_;
};
