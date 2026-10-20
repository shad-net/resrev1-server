// SPDX-FileCopyrightText: Copyright 2026 Resident Evil Revelations Custom Server Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "schedule_dialog.h"

#include "event_editor.h"
#include "event_presets.h"
#include "window_editor.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

ScheduleDialog::ScheduleDialog(const QJsonObject& entry, QWidget* parent) : QDialog(parent) {
    setWindowTitle(entry.isEmpty() ? tr("Add scheduled event") : tr("Edit scheduled event"));

    name_ = new QLineEdit(entry.value("name").toString());
    name_->setPlaceholderText(tr("Shown to players, such as \"Ooze weekend\""));
    description_ = new QLineEdit(entry.value("description").toString());
    description_->setPlaceholderText(tr("Optional sentence for players"));

    window_ = new WindowEditor(false);
    if (!entry.isEmpty()) {
        window_->setWindow(entry);
    }

    preset_ = new QComboBox;
    preset_->addItem(tr("Fill from a preset..."));
    for (const Presets::Preset& p : Presets::events()) {
        preset_->addItem(Presets::label(p));
    }

    event_ = new EventEditor;
    event_->setEvent(entry.value("event").toObject());
    auto* scroll = new QScrollArea;
    scroll->setWidget(event_);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto* form = new QFormLayout;
    form->addRow(tr("Name"), name_);
    form->addRow(tr("Text for players"), description_);

    auto* whenBox = new QGroupBox(tr("When"));
    auto* whenLayout = new QVBoxLayout(whenBox);
    whenLayout->addWidget(window_);

    auto* eventBox = new QGroupBox(tr("Event"));
    auto* eventLayout = new QVBoxLayout(eventBox);
    eventLayout->addWidget(preset_);
    eventLayout->addWidget(scroll, 1);

    error_ = new QLabel;
    error_->setStyleSheet(QStringLiteral("color: #c0392b"));
    error_->setWordWrap(true);
    error_->hide();

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &ScheduleDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(whenBox);
    layout->addWidget(eventBox, 1);
    layout->addWidget(error_);
    layout->addWidget(buttons);

    connect(preset_, qOverload<int>(&QComboBox::activated), this, [this](int index) {
        if (index > 0) {
            const Presets::Preset& p = Presets::events()[index - 1];
            event_->setEvent(p.event);
            if (name_->text().trimmed().isEmpty()) {
                name_->setText(p.name);
            }
        }
        preset_->setCurrentIndex(0);
    });
    resize(1000, 760);
}

void ScheduleDialog::accept() {
    QString why;
    const auto event = event_->event(&why);
    if (!event) {
        why = tr("Event: %1").arg(why);
    } else if (event->isEmpty()) {
        why = tr("The event is empty; fill in at least one block or pick a preset.");
    } else if (name_->text().trimmed().isEmpty()) {
        why = tr("Give the entry a name.");
    }
    if (!why.isEmpty()) {
        error_->setText(why);
        error_->show();
        return;
    }
    QJsonObject entry = window_->window();
    entry.insert("name", name_->text().trimmed());
    if (!description_->text().trimmed().isEmpty()) {
        entry.insert("description", description_->text().trimmed());
    }
    entry.insert("event", *event);
    entry_ = entry;
    QDialog::accept();
}
