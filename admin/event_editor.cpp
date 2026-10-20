// SPDX-FileCopyrightText: Copyright 2026 Resident Evil Revelations Custom Server Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "event_editor.h"
#include "reward_catalog.h"
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QStyledItemDelegate>

#include <cmath>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
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
        const QString text = box->currentText().trimmed().section(' ', 0, 0);
        model->setData(index, text);
    }

private:
    QVector<QPair<int, QString>> names_;
};

QGroupBox* section(const QString& title, const QString& help, QLayout* fields,
                   const QString& details = {}) {
    auto* box = new QGroupBox(title);
    auto* wrap = new QVBoxLayout(box);
    auto* label = new QLabel(help);
    label->setWordWrap(true);
    wrap->addWidget(label);
    wrap->addLayout(fields);
    wrap->addStretch(1); // keep the fields together at the top when the row is taller
    if (!details.isEmpty()) {
        box->setToolTip(details);
    }
    return box;
}

QString untested(const QString& title) {
    return title + QObject::tr("  (not tested in game yet)");
}

QWidget* labelled(const QString& text, QWidget* field) {
    auto* w = new QWidget;
    auto* l = new QHBoxLayout(w);
    l->setContentsMargins(0, 0, 0, 0);
    auto* label = new QLabel(text);
    label->setMinimumWidth(170);
    l->addWidget(label);
    l->addWidget(field, 1);
    return w;
}

} // namespace

QDoubleSpinBox* EventEditor::number(const char* key, double max, double scale,
                                    const QString& prefix, const QString& suffix, int decimals) {
    auto* box = new QDoubleSpinBox;
    box->setDecimals(decimals);
    box->setRange(-1, max);
    box->setSpecialValueText(tr("not set"));
    box->setValue(-1);
    box->setPrefix(prefix);
    box->setSuffix(suffix);
    box->setToolTip(tr("Sent as %1").arg(QLatin1String(key)));
    numbers_.append({key, box, scale});
    return box;
}

QComboBox* EventEditor::choice(const char* key, const Names& items) {
    auto* box = new QComboBox;
    for (const auto& [value, text] : items) {
        box->addItem(text, value);
    }
    box->setToolTip(tr("Sent as %1").arg(QLatin1String(key)));
    choices_.append({key, box});
    connect(box, qOverload<int>(&QComboBox::currentIndexChanged), this,
            &EventEditor::updateEnabled);
    return box;
}

QWidget* EventEditor::table(const char* keyA, const char* keyB, const QString& headerA,
                            const QString& headerB, int maxRows, bool stringA, qint64 maxA,
                            qint64 maxB, double scaleB, const Names& namesA) {
    auto* t = new QTableWidget(0, 2);
    t->setHorizontalHeaderLabels({headerA, headerB});
    t->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    t->verticalHeader()->setDefaultSectionSize(24);
    t->setMinimumHeight(110);
    t->setMaximumHeight(170);
    t->setToolTip(tr("Sent as %1 and %2, up to %3 rows")
                      .arg(QLatin1String(keyA), QLatin1String(keyB))
                      .arg(maxRows));
    tables_.append({keyA, keyB, stringA, maxA, maxB, scaleB, t});
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
    Names enemies;
    for (int i = 0; i < Catalog::raidEnemies().size(); ++i) {
        enemies.append({i, Catalog::raidEnemies()[i]});
    }
    Names shopItems;
    for (const Catalog::NamedId& item : Catalog::shopItems()) {
        shopItems.append({item.id, QString::fromLatin1(item.name)});
    }

    // Kill bonus (subjugation).
    auto* kills = new QVBoxLayout;
    kills->addWidget(table("subjugation_enemy_id", "subjugation_exp", tr("Enemy"), tr("Bonus %"),
                           20, false, U8Max, 5000, 10, enemies));
    auto* killSection = section(
        tr("Bonus EXP for killing enemies"),
        tr("Each kill of a listed enemy gives extra EXP: +100 % doubles it, +500 % is the most. "
           "Only the kill EXP "
           "grows, so a cleared stage gains less than the number suggests. Enemy 0 is the Ooze."),
        kills,
        tr("Per kill: EXP x (100 + bonus) / 100, fractions dropped. Tested on Raid Stage 1: +500 % "
           "on Oozes took a "
           "clear from about 390 to 515 EXP and a failed 8-kill run from 75 to 386."));

    // Special event: one kind at a time.
    special_ = choice("special_event_id", {{-1, tr("Off")},
                                           {1, tr("Bonus EXP on Chasm")},
                                           {2, tr("Bonus EXP on Trench")},
                                           {3, tr("Bonus EXP on Abyss")},
                                           {8, tr("More Wanted enemies")},
                                           {4, tr("Kind 4 (no known effect)")},
                                           {5, tr("Kind 5 (no known effect)")},
                                           {6, tr("Kind 6 (no known effect)")},
                                           {7, tr("Kind 7 (no known effect)")},
                                           {9, tr("Kind 9 (no known effect)")}});
    specialExp_ = labelled(tr("EXP bonus"), number("special_exp_rate", 500, 10, tr("+"), tr(" %")));
    specialWanted_ = labelled(tr("Extra Wanted chance"),
                              number("special_wanted_rate", 100, 10, tr("+"), tr(" %")));
    auto* specialFields = new QVBoxLayout;
    specialFields->addWidget(labelled(tr("Kind"), special_));
    specialFields->addWidget(specialExp_);
    specialFields->addWidget(specialWanted_);
    specialFields->addWidget(labelled(tr("Event number"), number("special_serial_id", I32Max, 1)));
    auto* specialSection =
        section(untested(tr("Special event")),
                tr("Bonus EXP on one difficulty (+100 % doubles all the EXP from the results "
                   "screen), or a higher chance of "
                   "a Wanted enemy on each stage (+100 % means one every time)."),
                specialFields,
                tr("special_event_id 1-3: EXP x (100 + bonus) / 100 on Chasm, Trench or Abyss. 8: "
                   "the bonus is added to the "
                   "stage's own Wanted chance, capped at 100 %. The event number is kept by the "
                   "game; leave it at 1."));

    // Raid shop sale (weekend).
    shop_ = choice("weekend_type", {{-1, tr("Off")},
                                    {1, tr("Weapons on sale")},
                                    {3, tr("Item group X on sale")},
                                    {5, tr("Item group Y on sale")},
                                    {2, tr("Special prices, list A")},
                                    {4, tr("Special prices, list B")}});
    shopShare_ = labelled(tr("Players pay"), number("weekend_discount_rate", 100, 10, QString(),
                                                    tr(" % of the price")));
    shopItems_ = table("weekend_item_item_id", "weekend_item_price", tr("Item"), tr("Price"), 5,
                       false, U8Max, I32Max, 1, shopItems);
    auto* shopFields = new QVBoxLayout;
    shopFields->addWidget(labelled(tr("Sale"), shop_));
    shopFields->addWidget(shopShare_);
    shopFields->addWidget(shopItems_);
    auto* shopSection = section(untested(tr("Raid shop sale")),
                                tr("Lower prices in the Raid shop: 50 % means half price, 0 % "
                                   "free. The two lists set a price per item instead."),
                                shopFields,
                                tr("weekend_type 1 = weapons (0x8005 codes), 3 = group X, items "
                                   "0x8008 (except 0x1a), 5 = group Y, items 0x8007. "
                                   "Price x share / 100."));

    // Extra discount (discount event).
    discount_ = choice("discount_event_id",
                       {{-1, tr("Off")}, {1, tr("Item group A")}, {2, tr("Item group B")}});
    discountShare_ = labelled(
        tr("Players pay"), number("discount_exp_rate", 100, 10, QString(), tr(" % of the price")));
    auto* discountFields = new QVBoxLayout;
    discountFields->addWidget(labelled(tr("Items"), discount_));
    discountFields->addWidget(discountShare_);
    auto* discountSection =
        section(untested(tr("Extra item discount")),
                tr("A second price cut for one more group of shop items. The game calls this an "
                   "EXP rate, but it changes prices."),
                discountFields,
                tr("discount_event_id 1 = items 0x800810xx-0x800814xx, 2 = items 0x800815xx."));

    // Wanted names (collaboration).
    auto* names = new QVBoxLayout;
    names->addWidget(table("collaboration_wanted_name", "collaboration_wanted_serial_id",
                           tr("Name"), tr("Number"), 10, true, 15, I32Max, 1));
    auto* namesSection =
        section(untested(tr("Wanted enemy names")),
                tr("Names shown on Wanted enemies (up to 15 plain letters or digits each). To be "
                   "sure a Wanted enemy appears, "
                   "also set the special event to \"More Wanted enemies\". Leave the number at 1."),
                names);

    // Participation event: effect not found yet.
    auto* other = new QVBoxLayout;
    other->addWidget(
        labelled(tr("Participation kind"), choice("participate_event_id", {{-1, tr("Off")},
                                                                           {1, tr("1")},
                                                                           {2, tr("2")},
                                                                           {3, tr("3")},
                                                                           {4, tr("4")},
                                                                           {5, tr("5")}})));
    other->addWidget(
        labelled(tr("Participation number"), number("participate_serial_id", I32Max, 1)));
    auto* otherSection = section(
        tr("Other (effect unknown)"),
        tr("The game accepts these but what they do is not known yet. Leave them off."), other);

    auto* grid = new QGridLayout(this);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->addWidget(killSection, 0, 0);
    grid->addWidget(specialSection, 0, 1);
    grid->addWidget(shopSection, 1, 0);
    grid->addWidget(discountSection, 1, 1);
    grid->addWidget(namesSection, 2, 0);
    grid->addWidget(otherSection, 2, 1);
    grid->setRowStretch(3, 1);
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);
    updateEnabled();
}

void EventEditor::updateEnabled() {
    if (!special_ || !shop_ || !discount_) {
        return; // still being built
    }
    const int kind = special_->currentData().toInt();
    specialExp_->setEnabled(kind >= 1 && kind <= 3);
    specialWanted_->setEnabled(kind == 8);
    const int sale = shop_->currentData().toInt();
    shopShare_->setEnabled(sale == 1 || sale == 3 || sale == 5);
    shopItems_->setEnabled(sale == 2 || sale == 4);
    discountShare_->setEnabled(discount_->currentData().toInt() > 0);
}

void EventEditor::setEvent(const QJsonObject& event) {
    for (const Number& n : numbers_) {
        n.box->setValue(event.contains(n.key) ? event.value(n.key).toDouble() / n.scale : -1);
    }
    for (const Choice& c : choices_) {
        const int value = event.contains(c.key) ? int(event.value(c.key).toInteger()) : -1;
        int index = c.box->findData(value);
        if (index < 0) {
            // A value the list does not name: keep it, so saving does not change it.
            c.box->addItem(tr("Value %1 (unknown)").arg(value), value);
            index = c.box->count() - 1;
        }
        c.box->setCurrentIndex(index);
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
            t.table->setItem(r, 1,
                             new QTableWidgetItem(vb.isDouble()
                                                      ? QString::number(vb.toDouble() / t.scaleB)
                                                      : QString()));
        }
    }
    updateEnabled();
}

std::optional<QJsonObject> EventEditor::event(QString* error) const {
    QJsonObject out;
    for (const Number& n : numbers_) {
        // A field that does not apply to the chosen kind is left out.
        if (n.box->value() >= 0 && n.box->parentWidget() && n.box->parentWidget()->isEnabled()) {
            out.insert(n.key, qint64(std::llround(n.box->value() * n.scale)));
        }
    }
    for (const Choice& c : choices_) {
        const int value = c.box->currentData().toInt();
        if (value >= 0) {
            out.insert(c.key, value);
        }
    }
    for (const Table& t : tables_) {
        if (!t.table->isEnabled()) {
            continue;
        }
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
                tr("%1, row %2: ").arg(t.table->horizontalHeaderItem(0)->text()).arg(r + 1);
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
                    *error =
                        where + tr("pick an entry from the list (or a number 0 to %1)").arg(t.maxA);
                    return std::nullopt;
                }
                a.append(v);
            }
            bool ok = false;
            const double shown = tb.toDouble(&ok);
            const qint64 v = std::llround(shown * t.scaleB);
            if (!ok || v < 0 || v > t.maxB) {
                *error = where + tr("the %1 must be a number from 0 to %2")
                                     .arg(t.table->horizontalHeaderItem(1)->text().toLower())
                                     .arg(double(t.maxB) / t.scaleB);
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
