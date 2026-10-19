// SPDX-FileCopyrightText: Copyright 2026 Resident Evil Revelations Custom Server Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "reward_dialog.h"

#include "reward_catalog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace {

QSpinBox* spin(int min, int max, int value) {
    auto* s = new QSpinBox;
    s->setRange(min, max);
    s->setValue(value);
    return s;
}

QComboBox* namedCombo(const QStringList& names) {
    auto* c = new QComboBox;
    for (int i = 0; i < names.size(); ++i) {
        c->addItem(QStringLiteral("%1  %2").arg(i, 2).arg(names[i]), i);
    }
    c->setMaxVisibleItems(20);
    return c;
}

QWidget* page(QFormLayout* form) {
    auto* w = new QWidget;
    form->setContentsMargins(0, 0, 0, 0);
    w->setLayout(form);
    return w;
}

} // namespace

RewardDialog::RewardDialog(qint64 serial, const QStringList& players, QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(tr("Add reward"));

    serial_ = spin(1, 2147483647, int(serial));
    type_ = new QComboBox;
    type_->addItem(tr("Supply (relief)"), QStringLiteral("relief"));
    type_->addItem(tr("Custom part (parts)"), QStringLiteral("parts"));
    type_->addItem(tr("Weapon"), QStringLiteral("weapon"));
    type_->addItem(tr("Random part (gacha)"), QStringLiteral("gacha"));

    relief_ = namedCombo(Catalog::reliefNames());
    reliefCount_ = spin(0, 255, 5);
    auto* reliefForm = new QFormLayout;
    reliefForm->addRow(tr("Supply"), relief_);
    reliefForm->addRow(tr("Count"), reliefCount_);

    part_ = namedCombo(Catalog::partNames());
    auto* partForm = new QFormLayout;
    partForm->addRow(tr("Part"), part_);

    weapon_ = namedCombo(Catalog::weaponNames());
    rarity_ = spin(0, 255, 1);
    slot_ = spin(0, 255, 3);
    randomTag_ = new QCheckBox(tr("Add a random tag (such as \"Speed Load\")"));
    auto* weaponForm = new QFormLayout;
    weaponForm->addRow(tr("Weapon"), weapon_);
    weaponForm->addRow(tr("Level (rarity)"), rarity_);
    weaponForm->addRow(tr("Slots"), slot_);
    weaponForm->addRow(QString(), randomTag_);

    gachaType_ = spin(0, 255, 1);
    gachaGrade_ = spin(0, 255, 1);
    auto* gachaForm = new QFormLayout;
    gachaForm->addRow(tr("Gacha type"), gachaType_);
    gachaForm->addRow(tr("Grade (part level)"), gachaGrade_);

    pages_ = new QStackedWidget;
    pages_->addWidget(page(reliefForm));
    pages_->addWidget(page(partForm));
    pages_->addWidget(page(weaponForm));
    pages_->addWidget(page(gachaForm));

    players_ = new QLineEdit(players.join(QStringLiteral(", ")));
    players_->setPlaceholderText(tr("everyone"));
    players_->setToolTip(tr("Comma-separated shadNet online IDs. "
                            "Leave empty to offer the reward to every player."));

    preview_ = new QPlainTextEdit;
    preview_->setReadOnly(true);
    preview_->setMaximumHeight(70);
    QFont mono(QStringLiteral("monospace"));
    mono.setStyleHint(QFont::Monospace);
    preview_->setFont(mono);

    auto* form = new QFormLayout;
    form->addRow(tr("Serial"), serial_);
    form->addRow(tr("Type"), type_);
    form->addRow(pages_);
    form->addRow(tr("For players"), players_);
    form->addRow(tr("JSON"), preview_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Add"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);

    connect(type_, qOverload<int>(&QComboBox::currentIndexChanged), pages_,
            &QStackedWidget::setCurrentIndex);
    for (QComboBox* c : {type_, relief_, part_, weapon_}) {
        connect(c, qOverload<int>(&QComboBox::currentIndexChanged), this,
                &RewardDialog::updatePreview);
    }
    for (QSpinBox* s : {serial_, reliefCount_, gachaType_, gachaGrade_, rarity_, slot_}) {
        connect(s, qOverload<int>(&QSpinBox::valueChanged), this, &RewardDialog::updatePreview);
    }
    connect(randomTag_, &QCheckBox::toggled, this, &RewardDialog::updatePreview);
    connect(players_, &QLineEdit::textChanged, this, &RewardDialog::updatePreview);
    if (!players.isEmpty()) {
        type_->setCurrentIndex(2);
    }
    updatePreview();
    resize(460, sizeHint().height());
}

QJsonObject RewardDialog::reward() const {
    QJsonObject r;
    r.insert("serial", serial_->value());
    const QString type = type_->currentData().toString();
    r.insert("type", type);
    if (type == QLatin1String("relief")) {
        r.insert("relief_type", relief_->currentData().toInt());
        r.insert("relief_count", reliefCount_->value());
    } else if (type == QLatin1String("parts")) {
        r.insert("parts_id", part_->currentData().toInt());
    } else if (type == QLatin1String("weapon")) {
        r.insert("weapon_id", weapon_->currentData().toInt());
        r.insert("weapon_rarity", rarity_->value());
        r.insert("weapon_slot", slot_->value());
        if (randomTag_->isChecked()) {
            r.insert("weapon_randomtag", true);
        }
    } else {
        r.insert("gacha_type", gachaType_->value());
        r.insert("gacha_grade", gachaGrade_->value());
    }
    QJsonArray players;
    for (const QString& p : players_->text().split(',', Qt::SkipEmptyParts)) {
        if (!p.trimmed().isEmpty()) {
            players.append(p.trimmed());
        }
    }
    if (!players.isEmpty()) {
        r.insert("players", players);
    }
    return r;
}

void RewardDialog::updatePreview() {
    preview_->setPlainText(
        QString::fromUtf8(QJsonDocument(reward()).toJson(QJsonDocument::Compact)));
}
