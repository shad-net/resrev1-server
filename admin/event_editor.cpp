// SPDX-FileCopyrightText: Copyright 2026 Resident Evil Revelations Custom Server Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "event_editor.h"

#include "reward_catalog.h"

#include <QComboBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {

constexpr qint64 U8Max = 255;
constexpr qint64 I32Max = 2147483647;

class NamedIdDelegate : public QStyledItemDelegate {
public:
    NamedIdDelegate(QVector<QPair<int, QString>> names, QObject* parent)
        : QStyledItemDelegate(parent), names_(std::move(names)) {}

    QString displayText(const QVariant& value, const QLocale& locale) const override {
        const QString text = value.toString().trimmed();
        bool ok = false;
        const int id = text.toInt(&ok);
        if (ok) {
            for (const auto& [n, name] : names_) {
                if (n == id) {
                    return QStringLiteral("%1  %2").arg(id).arg(name);
                }
            }
        }
        return QStyledItemDelegate::displayText(value, locale);
    }

    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem&,
                          const QModelIndex&) const override {
        auto* box = new QComboBox(parent);
        box->setEditable(true);
        box->setInsertPolicy(QComboBox::NoInsert);
        box->setMaxVisibleItems(20);
        for (const auto& [id, name] : names_) {
            box->addItem(QStringLiteral("%1  %2").arg(id).arg(name), id);
        }
        return box;
    }

    void setEditorData(QWidget* editor, const QModelIndex& index) const override {
        auto* box = static_cast<QComboBox*>(editor);
        const QString text = index.data().toString().trimmed();
        const int found = text.isEmpty() ? -1 : box->findData(text.toInt());
        if (found >= 0) {
            box->setCurrentIndex(found);
        } else {
            box->setEditText(text);
        }
    }

    void setModelData(QWidget* editor, QAbstractItemModel* model,
                      const QModelIndex& index) const override {
        auto* box = static_cast<QComboBox*>(editor);
        // The leading number is the ID, whether picked from the list or typed.
        const QString text = box->currentText().trimmed().section(' ', 0, 0);
        model->setData(index, text);
    }

private:
    QVector<QPair<int, QString>> names_;
};

QGroupBox* group(const QString& title, QLayout* layout, const QString& note = {}) {
    auto* box = new QGroupBox(title);
    if (!note.isEmpty()) {
        auto* wrap = new QVBoxLayout;
        auto* label = new QLabel(note);
        label->setWordWrap(true);
        label->setForegroundRole(QPalette::PlaceholderText);
        wrap->addWidget(label);
        wrap->addLayout(layout);
        box->setLayout(wrap);
    } else {
        box->setLayout(layout);
    }
    return box;
}

} // namespace

QSpinBox* EventEditor::scalar(const char* key, qint64 max) {
    auto* box = new QSpinBox;
    box->setRange(-1, int(max));
    box->setSpecialValueText(tr("not set"));
    box->setValue(-1);
    box->setToolTip(QString::fromLatin1(key));
    scalars_.append({key, box});
    return box;
}

QWidget* EventEditor::table(const char* keyA, const char* keyB, const QString& headerA,
                            const QString& headerB, int maxRows, bool stringA, qint64 maxA,
                            qint64 maxB, const Names& namesA) {
    auto* t = new QTableWidget(0, 2);
    t->setHorizontalHeaderLabels({headerA, headerB});
    t->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    t->verticalHeader()->setDefaultSectionSize(24);
    t->setMinimumHeight(110);
    t->setMaximumHeight(170);
    t->setToolTip(QStringLiteral("%1 / %2, up to %3 rows")
                      .arg(QLatin1String(keyA), QLatin1String(keyB))
                      .arg(maxRows));
    tables_.append({keyA, keyB, stringA, maxA, maxB, t});
    if (!namesA.isEmpty()) {
        t->setItemDelegateForColumn(0, new NamedIdDelegate(namesA, t));
    }

    auto* add = new QPushButton(tr("Add row"));
    auto* remove = new QPushButton(tr("Remove row"));
    connect(add, &QPushButton::clicked, t, [t, maxRows, add] {
        if (t->rowCount() < maxRows) {
            t->insertRow(t->rowCount());
            t->setItem(t->rowCount() - 1, 0, new QTableWidgetItem);
            t->setItem(t->rowCount() - 1, 1, new QTableWidgetItem);
            t->editItem(t->item(t->rowCount() - 1, 0));
        }
        add->setEnabled(t->rowCount() < maxRows);
    });
    connect(remove, &QPushButton::clicked, t, [t, maxRows, add] {
        const int row = t->currentRow() >= 0 ? t->currentRow() : t->rowCount() - 1;
        if (row >= 0) {
            t->removeRow(row);
        }
        add->setEnabled(t->rowCount() < maxRows);
    });
    connect(t->model(), &QAbstractItemModel::rowsInserted, add,
            [t, maxRows, add] { add->setEnabled(t->rowCount() < maxRows); });

    auto* buttons = new QHBoxLayout;
    buttons->addWidget(add);
    buttons->addWidget(remove);
    buttons->addStretch();
    auto* w = new QWidget;
    auto* layout = new QVBoxLayout(w);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(t);
    layout->addLayout(buttons);
    return w;
}

EventEditor::EventEditor(QWidget* parent) : QWidget(parent) {
    const QString tenths = tr("Rates and EXP are in tenths: 15 means x1.5.");

    Names enemies;
    for (int i = 0; i < Catalog::raidEnemies().size(); ++i) {
        enemies.append({i, Catalog::raidEnemies()[i] + QStringLiteral(" ?")});
    }
    Names shop;
    for (const Catalog::NamedId& item : Catalog::shopItems()) {
        shop.append({item.id, QString::fromLatin1(item.name)});
    }

    auto* subjugation = new QVBoxLayout;
    subjugation->addWidget(table("subjugation_enemy_id", "subjugation_exp", tr("Enemy ID"),
                                 tr("EXP (tenths)"), 20, false, U8Max, I32Max, enemies));

    auto* weekend = new QFormLayout;
    weekend->addRow(tr("Type"), scalar("weekend_type", U8Max));
    weekend->addRow(tr("Discount rate"), scalar("weekend_discount_rate", I32Max));
    weekend->addRow(table("weekend_item_item_id", "weekend_item_price", tr("Item ID"), tr("Price"),
                          5, false, U8Max, I32Max, shop));

    auto* participate = new QFormLayout;
    participate->addRow(tr("Serial"), scalar("participate_serial_id", I32Max));
    participate->addRow(tr("Event ID"), scalar("participate_event_id", U8Max));

    auto* special = new QFormLayout;
    special->addRow(tr("Serial"), scalar("special_serial_id", I32Max));
    special->addRow(tr("Event ID"), scalar("special_event_id", U8Max));
    special->addRow(tr("EXP rate"), scalar("special_exp_rate", I32Max));
    special->addRow(tr("Wanted rate"), scalar("special_wanted_rate", I32Max));

    auto* discount = new QFormLayout;
    discount->addRow(tr("Event ID"), scalar("discount_event_id", U8Max));
    discount->addRow(tr("EXP rate"), scalar("discount_exp_rate", I32Max));

    auto* collaboration = new QVBoxLayout;
    collaboration->addWidget(table("collaboration_wanted_name", "collaboration_wanted_serial_id",
                                   tr("Name"), tr("Serial"), 10, true, 15, I32Max));

    auto* grid = new QGridLayout(this);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->addWidget(group(tr("Subjugation"), subjugation,
                          tr("Bonus EXP for defeating these enemies. Enemy IDs are not mapped yet: "
                             "the names are the "
                             "Raid enemy list order, a guess (marked ?).") +
                              ' ' + tenths),
                    0, 0, 3, 1);
    grid->addWidget(group(tr("Weekend shop"), weekend,
                          tr("Shop discount and item prices. Items are named from the Raid shop "
                             "list; that the event uses "
                             "these IDs is not confirmed yet.")),
                    0, 1, 3, 1);
    grid->addWidget(group(tr("Participation event"), participate), 0, 2);
    grid->addWidget(group(tr("Special event"), special, tenths), 1, 2);
    grid->addWidget(group(tr("Discount event"), discount), 2, 2);
    grid->addWidget(group(tr("Collaboration wanted"), collaboration,
                          tr("Names: ASCII, at most 15 characters.")),
                    3, 0, 1, 3);
    grid->setRowStretch(4, 1);
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);
    grid->setColumnStretch(2, 1);
}

void EventEditor::setEvent(const QJsonObject& event) {
    for (const Scalar& s : scalars_) {
        s.box->setValue(event.contains(s.key) ? int(event.value(s.key).toInteger()) : -1);
    }
    for (const Table& t : tables_) {
        const QJsonArray a = event.value(t.keyA).toArray();
        const QJsonArray b = event.value(t.keyB).toArray();
        const int rows = int(std::max(a.size(), b.size()));
        t.table->setRowCount(0);
        t.table->setRowCount(rows);
        for (int r = 0; r < rows; ++r) {
            const QJsonValue va = r < a.size() ? a[r] : QJsonValue();
            const QJsonValue vb = r < b.size() ? b[r] : QJsonValue();
            t.table->setItem(r, 0,
                             new QTableWidgetItem(va.isString()   ? va.toString()
                                                  : va.isDouble() ? QString::number(va.toInteger())
                                                                  : QString()));
            t.table->setItem(
                r, 1,
                new QTableWidgetItem(vb.isDouble() ? QString::number(vb.toInteger()) : QString()));
        }
    }
}

std::optional<QJsonObject> EventEditor::event(QString* error) const {
    QJsonObject out;
    for (const Scalar& s : scalars_) {
        if (s.box->value() >= 0) {
            out.insert(s.key, s.box->value());
        }
    }
    for (const Table& t : tables_) {
        QJsonArray a;
        QJsonArray b;
        for (int r = 0; r < t.table->rowCount(); ++r) {
            const QString ta =
                t.table->item(r, 0) ? t.table->item(r, 0)->text().trimmed() : QString();
            const QString tb =
                t.table->item(r, 1) ? t.table->item(r, 1)->text().trimmed() : QString();
            if (ta.isEmpty() && tb.isEmpty()) {
                continue;
            }
            const QString where =
                QStringLiteral("%1 row %2: ").arg(QLatin1String(t.keyA)).arg(r + 1);
            if (t.stringA) {
                if (ta.isEmpty() || ta.size() > t.maxA) {
                    *error = where + tr("the name must be 1 to %1 characters").arg(t.maxA);
                    return std::nullopt;
                }
                a.append(ta);
            } else {
                bool ok = false;
                const qint64 v = ta.toLongLong(&ok);
                if (!ok || v < 0 || v > t.maxA) {
                    *error = where + tr("the first column must be a number 0..%1").arg(t.maxA);
                    return std::nullopt;
                }
                a.append(v);
            }
            bool ok = false;
            const qint64 v = tb.toLongLong(&ok);
            if (!ok || v < 0 || v > t.maxB) {
                *error = where + tr("the second column must be a number 0..%1").arg(t.maxB);
                return std::nullopt;
            }
            b.append(v);
        }
        if (!a.isEmpty()) {
            out.insert(t.keyA, a);
            out.insert(t.keyB, b);
        }
    }
    return out;
}
