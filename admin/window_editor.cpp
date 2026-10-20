// SPDX-FileCopyrightText: Copyright 2026 Resident Evil Revelations Custom Server Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "window_editor.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QDateTimeEdit>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QRadioButton>
#include <QStackedWidget>
#include <QTimeEdit>
#include <QTimeZone>
#include <QVBoxLayout>

namespace {

const QStringList& dayKeys() {
    static const QStringList days = {"mon", "tue", "wed", "thu", "fri", "sat", "sun"};
    return days;
}

QComboBox* dayCombo() {
    auto* c = new QComboBox;
    const QStringList names = {QObject::tr("Monday"),    QObject::tr("Tuesday"),
                               QObject::tr("Wednesday"), QObject::tr("Thursday"),
                               QObject::tr("Friday"),    QObject::tr("Saturday"),
                               QObject::tr("Sunday")};
    for (int i = 0; i < 7; ++i) {
        c->addItem(names[i], dayKeys()[i]);
    }
    return c;
}

QTimeEdit* timeEdit() {
    auto* t = new QTimeEdit(QTime(0, 0));
    t->setDisplayFormat(QStringLiteral("HH:mm"));
    return t;
}

QDateTimeEdit* dateTimeEdit(const QDateTime& value) {
    auto* d = new QDateTimeEdit(value);
    d->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm"));
    d->setCalendarPopup(true);
    return d;
}

QWidget* row(std::initializer_list<QWidget*> widgets) {
    auto* w = new QWidget;
    auto* l = new QHBoxLayout(w);
    l->setContentsMargins(0, 0, 0, 0);
    for (QWidget* x : widgets) {
        l->addWidget(x);
    }
    l->addStretch();
    return w;
}

// "sat 00:00" -> day index and time.
bool splitWeekTime(const QString& text, int* day, QTime* time) {
    const QStringList parts = text.trimmed().split(' ', Qt::SkipEmptyParts);
    if (parts.size() != 2) {
        return false;
    }
    *day = int(dayKeys().indexOf(parts[0].left(3).toLower()));
    *time = QTime::fromString(parts[1], QStringLiteral("H:mm"));
    return *day >= 0 && time->isValid();
}

} // namespace

WindowEditor::WindowEditor(bool allowAlways, QWidget* parent) : QWidget(parent) {
    auto* group = new QButtonGroup(this);
    auto* modes = new QHBoxLayout;
    modes->setContentsMargins(0, 0, 0, 0);
    if (allowAlways) {
        always_ = new QRadioButton(tr("Always"));
        group->addButton(always_);
        modes->addWidget(always_);
    }
    once_ = new QRadioButton(tr("Between two dates"));
    weekly_ = new QRadioButton(tr("Every week"));
    group->addButton(once_);
    group->addButton(weekly_);
    modes->addWidget(once_);
    modes->addWidget(weekly_);
    modes->addStretch();

    zone_ = new QComboBox;
    zone_->setEditable(true);
    QStringList zones = {QString::fromUtf8(QTimeZone::systemTimeZoneId()),
                         QStringLiteral("UTC"),
                         QStringLiteral("Europe/Athens"),
                         QStringLiteral("Europe/London"),
                         QStringLiteral("Europe/Berlin"),
                         QStringLiteral("America/New_York"),
                         QStringLiteral("America/Los_Angeles"),
                         QStringLiteral("Asia/Tokyo")};
    zones.removeDuplicates();
    zone_->addItems(zones);
    zone_->setToolTip(tr("IANA time zone name; times follow its clock, daylight saving included"));

    const QDateTime today(QDate::currentDate(), QTime(0, 0));
    onceStart_ = dateTimeEdit(today);
    onceEnd_ = dateTimeEdit(today.addDays(2));
    auto* onceForm = new QFormLayout;
    onceForm->setContentsMargins(0, 0, 0, 0);
    onceForm->addRow(tr("From"), onceStart_);
    onceForm->addRow(tr("Until"), onceEnd_);
    auto* oncePage = new QWidget;
    oncePage->setLayout(onceForm);

    startDay_ = dayCombo();
    startDay_->setCurrentIndex(5);
    startTime_ = timeEdit();
    endDay_ = dayCombo();
    endTime_ = timeEdit();
    auto* weekForm = new QFormLayout;
    weekForm->setContentsMargins(0, 0, 0, 0);
    weekForm->addRow(tr("From"), row({startDay_, startTime_}));
    weekForm->addRow(tr("Until"), row({endDay_, endTime_}));
    auto* weekPage = new QWidget;
    weekPage->setLayout(weekForm);

    pages_ = new QStackedWidget;
    pages_->addWidget(new QWidget); // always
    pages_->addWidget(oncePage);
    pages_->addWidget(weekPage);

    auto* zoneRow = new QFormLayout;
    zoneRow->setContentsMargins(0, 0, 0, 0);
    zoneRow->addRow(tr("Time zone"), zone_);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addLayout(modes);
    layout->addWidget(pages_);
    layout->addLayout(zoneRow);

    const auto update = [this] {
        const int page = weekly_->isChecked() ? 2 : once_->isChecked() ? 1 : 0;
        pages_->setCurrentIndex(page);
        zone_->setEnabled(page != 0);
        emit changed();
    };
    connect(group, &QButtonGroup::buttonToggled, this, update);
    for (QComboBox* c : {startDay_, endDay_, zone_}) {
        connect(c, &QComboBox::currentTextChanged, this, &WindowEditor::changed);
    }
    for (QDateTimeEdit* e :
         {static_cast<QDateTimeEdit*>(onceStart_), static_cast<QDateTimeEdit*>(onceEnd_),
          static_cast<QDateTimeEdit*>(startTime_), static_cast<QDateTimeEdit*>(endTime_)}) {
        connect(e, &QDateTimeEdit::dateTimeChanged, this, &WindowEditor::changed);
    }
    (always_ ? always_ : weekly_)->setChecked(true);
    update();
}

void WindowEditor::setWindow(const QJsonObject& o) {
    if (o.contains("timezone")) {
        zone_->setCurrentText(o.value("timezone").toString());
    }
    if (o.contains("weekly")) {
        const QJsonObject w = o.value("weekly").toObject();
        int day = 0;
        QTime time;
        if (splitWeekTime(w.value("start").toString(), &day, &time)) {
            startDay_->setCurrentIndex(day);
            startTime_->setTime(time);
        }
        if (splitWeekTime(w.value("end").toString(), &day, &time)) {
            endDay_->setCurrentIndex(day);
            endTime_->setTime(time);
        }
        weekly_->setChecked(true);
    } else if (o.contains("start") || o.contains("end")) {
        // Shown as wall-clock time in the window's own time zone.
        const QTimeZone zone(zone_->currentText().toUtf8());
        const auto local = [&zone](const QString& text) {
            QDateTime t = QDateTime::fromString(text, Qt::ISODate);
            if (zone.isValid() && t.timeSpec() != Qt::LocalTime) {
                t = t.toTimeZone(zone);
            }
            return QDateTime(t.date(), t.time());
        };
        onceStart_->setDateTime(local(o.value("start").toString()));
        onceEnd_->setDateTime(local(o.value("end").toString()));
        once_->setChecked(true);
    } else if (always_) {
        always_->setChecked(true);
    }
}

QJsonObject WindowEditor::window() const {
    QJsonObject o;
    if (always_ && always_->isChecked()) {
        return o;
    }
    o.insert("timezone", zone_->currentText().trimmed());
    if (weekly_->isChecked()) {
        const auto text = [](QComboBox* day, QTimeEdit* time) {
            return QStringLiteral("%1 %2").arg(day->currentData().toString(),
                                               time->time().toString(QStringLiteral("HH:mm")));
        };
        o.insert("weekly", QJsonObject{{"start", text(startDay_, startTime_)},
                                       {"end", text(endDay_, endTime_)}});
    } else {
        const QString format = QStringLiteral("yyyy-MM-ddTHH:mm:ss");
        o.insert("start", onceStart_->dateTime().toString(format));
        o.insert("end", onceEnd_->dateTime().toString(format));
    }
    return o;
}
