// SPDX-FileCopyrightText: Copyright 2026 Resident Evil Revelations Custom Server Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QJsonObject>
#include <QWidget>

#include <optional>

class QComboBox;
class QDoubleSpinBox;
class QTableWidget;
class QWidget;

class EventEditor : public QWidget {
    Q_OBJECT
public:
    explicit EventEditor(QWidget* parent = nullptr);

    void setEvent(const QJsonObject& event);
    std::optional<QJsonObject> event(QString* error) const;

private:
    struct Number {
        const char* key;
        QDoubleSpinBox* box;
        double scale;
    };
    struct Choice {
        const char* key;
        QComboBox* box;
    };
    struct Table {
        const char* keyA;
        const char* keyB;
        bool stringA; // first column holds names instead of numbers
        qint64 maxA;
        qint64 maxB; // in JSON units
        double scaleB;
        QTableWidget* table;
    };
    using Names = QVector<QPair<int, QString>>;

    QDoubleSpinBox* number(const char* key, double max, double scale, const QString& prefix = {},
                           const QString& suffix = {}, int decimals = 0);
    QComboBox* choice(const char* key, const Names& items);
    QWidget* table(const char* keyA, const char* keyB, const QString& headerA,
                   const QString& headerB, int maxRows, bool stringA, qint64 maxA, qint64 maxB,
                   double scaleB, const Names& namesA = {});
    void updateEnabled();

    QVector<Number> numbers_;
    QVector<Choice> choices_;
    QVector<Table> tables_;

    QComboBox* special_ = nullptr;
    QWidget* specialExp_ = nullptr;
    QWidget* specialWanted_ = nullptr;
    QComboBox* shop_ = nullptr;
    QWidget* shopShare_ = nullptr;
    QWidget* shopItems_ = nullptr;
    QComboBox* discount_ = nullptr;
    QWidget* discountShare_ = nullptr;
};
