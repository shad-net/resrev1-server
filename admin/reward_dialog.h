// SPDX-FileCopyrightText: Copyright 2026 Resident Evil Revelations Custom Server Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QDialog>
#include <QJsonObject>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QSpinBox;
class QStackedWidget;

class RewardDialog : public QDialog {
    Q_OBJECT
public:
    RewardDialog(qint64 serial, const QStringList& players, QWidget* parent = nullptr);

    QJsonObject reward() const;

private:
    void updatePreview();

    QSpinBox* serial_;
    QComboBox* type_;
    QStackedWidget* pages_;
    QComboBox* relief_;
    QSpinBox* reliefCount_;
    QSpinBox* gachaType_;
    QSpinBox* gachaGrade_;
    QComboBox* part_;
    QComboBox* weapon_;
    QSpinBox* rarity_;
    QSpinBox* slot_;
    QCheckBox* randomTag_;
    QLineEdit* players_;
    QPlainTextEdit* preview_;
};
