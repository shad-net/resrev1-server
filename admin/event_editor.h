// SPDX-FileCopyrightText: Copyright 2026 Resident Evil Revelations Custom Server Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QJsonObject>
#include <QWidget>

#include <optional>

class QSpinBox;
class QTableWidget;

// Form for the regev event. Every field is optional: a "not set" number or an empty table
// leaves its key out, and an event with no keys means no event is running.
class EventEditor : public QWidget {
    Q_OBJECT
public:
    explicit EventEditor(QWidget* parent = nullptr);

    void setEvent(const QJsonObject& event);
    // The event as JSON, or nullopt with `error` set when a table cell is not valid.
    std::optional<QJsonObject> event(QString* error) const;

private:
    struct Scalar {
        const char* key;
        QSpinBox* box;
    };
    struct Table {
        const char* keyA;
        const char* keyB;
        bool stringA; // first column holds names instead of numbers
        qint64 maxA;
        qint64 maxB;
        QTableWidget* table;
    };

    using Names = QVector<QPair<int, QString>>;

    QSpinBox* scalar(const char* key, qint64 max);
    QWidget* table(const char* keyA, const char* keyB, const QString& headerA, const QString& headerB,
                   int maxRows, bool stringA, qint64 maxA, qint64 maxB, const Names& namesA = {});

    QVector<Scalar> scalars_;
    QVector<Table> tables_;
};
