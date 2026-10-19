// SPDX-FileCopyrightText: Copyright 2026 Resident Evil Revelations Custom Server Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "stats_dialog.h"

#include "api_client.h"

#include <QComboBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QLabel>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {

QString when(qint64 secs) {
    return secs > 0 ? QDateTime::fromSecsSinceEpoch(secs).toString(QStringLiteral("yyyy-MM-dd hh:mm")) : QStringLiteral("-");
}

QString duration(qint64 secs) {
    return QStringLiteral("%1:%2").arg(secs / 60).arg(secs % 60, 2, 10, QLatin1Char('0'));
}

// Seen: 1 for an A and 2 for a B. 0 and 3 are assumed to be S and C.
QString rankName(qint64 rank) {
    switch (rank) {
    case 0:
        return QStringLiteral("S?");
    case 1:
        return QStringLiteral("A");
    case 2:
        return QStringLiteral("B");
    case 3:
        return QStringLiteral("C?");
    default:
        return QString::number(rank);
    }
}

QString hex(qint64 v, int width) {
    return QStringLiteral("0x%1").arg(v, width, 16, QLatin1Char('0'));
}

QTableWidgetItem* text(const QString& s) {
    return new QTableWidgetItem(s);
}

QTableWidgetItem* number(qint64 v) {
    auto* item = new QTableWidgetItem;
    item->setData(Qt::DisplayRole, v);
    return item;
}

QTableWidget* table(const QStringList& headers) {
    auto* t = new QTableWidget(0, int(headers.size()));
    t->setHorizontalHeaderLabels(headers);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->verticalHeader()->hide();
    t->horizontalHeader()->setStretchLastSection(true);
    t->setAlternatingRowColors(true);
    return t;
}

QWidget* withNote(const QString& note, QWidget* content) {
    auto* w = new QWidget;
    auto* layout = new QVBoxLayout(w);
    auto* label = new QLabel(note);
    label->setWordWrap(true);
    label->setForegroundRole(QPalette::PlaceholderText);
    layout->addWidget(label);
    layout->addWidget(content);
    return w;
}

QString recordName(const QJsonObject& r) {
    return r.value("table").toString() + QString::number(r.value("index").toInteger());
}

} // namespace

StatsDialog::StatsDialog(ApiClient* api, qint64 playerId, const QString& player, QWidget* parent)
    : QDialog(parent), api_(api), playerId_(playerId) {
    setWindowTitle(tr("Stats: %1").arg(player));

    uploads_ = new QComboBox;
    uploads_->setMinimumContentsLength(28);
    status_ = new QLabel;
    auto* picker = new QHBoxLayout;
    picker->addWidget(new QLabel(tr("Upload")));
    picker->addWidget(uploads_);
    picker->addWidget(status_, 1);

    level_ = new QLabel;
    enemies_ = new QLabel;
    clearTime_ = new QLabel;
    stagesCleared_ = new QLabel;
    created_ = new QLabel;
    auto* totals = new QFormLayout;
    totals->addRow(tr("Level"), level_);
    totals->addRow(tr("Enemies routed"), enemies_);
    totals->addRow(tr("Total clear time"), clearTime_);
    totals->addRow(tr("Stages cleared (probably)"), stagesCleared_);
    totals->addRow(tr("Profile created"), created_);
    auto* totalsBox = new QGroupBox(tr("Totals"));
    totalsBox->setLayout(totals);

    const QStringList stageColumns = {tr("Record"), tr("Rank"), tr("Accuracy"), tr("Enemies"), tr("Clear time"),
                                      tr("Damage taken"), tr("Clears")};
    records_ = table(stageColumns + QStringList{tr("First set"), tr("Byte 1")});
    bests_ = table(stageColumns);
    changes_ = table({tr("Offset"), tr("Field"), tr("Before"), tr("After"), tr("Difference")});
    other_ = table({tr("Offset"), tr("Value"), tr("Hex")});

    tabs_ = new QTabWidget;
    tabs_->addTab(withNote(tr("The latest clear of each stage, as the game keeps it. Records Bn are Raid Mode (B0 and B1 "
                              "were Stage 1 and 2 on Chasm); An are probably the Campaign."),
                           records_),
                  tr("Stage records"));
    tabs_->addTab(withNote(tr("The best result per stage across this player's uploads up to the one shown. The game keeps "
                              "only the latest clear, so these come from the server's history."),
                           bests_),
                  tr("Personal bests"));
    tabs_->addTab(withNote(tr("Every 32-bit value that changed since the previous upload. Fields without a name are not "
                              "mapped yet; comparing them with what happened in the run helps map them."),
                           changes_),
                  tr("Changes"));
    tabs_->addTab(withNote(tr("Every non-zero value outside the mapped fields."), other_), tr("Not mapped"));

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(picker);
    layout->addWidget(totalsBox);
    layout->addWidget(tabs_, 1);
    layout->addWidget(buttons);
    resize(900, 620);

    connect(uploads_, qOverload<int>(&QComboBox::activated), this, [this](int index) {
        loadUpload(uploads_->itemData(index).toLongLong());
    });
    loadHistory();
}

void StatsDialog::loadHistory() {
    status_->setText(tr("Loading..."));
    api_->get(QStringLiteral("/api/players/%1/history").arg(playerId_), [this](const ApiClient::Reply& r) {
        if (!r.ok()) {
            status_->setText(tr("Could not load the history: %1").arg(r.error));
            return;
        }
        const QJsonArray list = r.json().array();
        uploads_->clear();
        for (qsizetype i = list.size() - 1; i >= 0; --i) {
            const QJsonObject h = list[i].toObject();
            uploads_->addItem(tr("%1  (#%2)").arg(when(h.value("received").toInteger())).arg(h.value("id").toInteger()),
                              h.value("id").toInteger());
        }
        if (uploads_->count() == 0) {
            status_->setText(tr("No uploads yet"));
            return;
        }
        uploads_->setCurrentIndex(0);
        loadUpload(uploads_->itemData(0).toLongLong());
    });
}

void StatsDialog::loadUpload(qint64 uploadId) {
    status_->setText(tr("Loading..."));
    api_->get(QStringLiteral("/api/players/%1/stats?upload=%2").arg(playerId_).arg(uploadId),
              [this](const ApiClient::Reply& r) {
                  if (!r.ok()) {
                      status_->setText(tr("Could not load the upload: %1").arg(r.error));
                      return;
                  }
                  display(r.json().object());
              });
}

void StatsDialog::display(const QJsonObject& s) {
    const int count = uploads_->count();
    status_->setText(count == 1 ? tr("1 upload kept") : tr("%1 uploads kept").arg(count));
    if (!s.value("valid").toBool()) {
        status_->setText(tr("This upload (%1 bytes) is not a player record this version understands.")
                             .arg(s.value("size").toInteger()));
        return;
    }
    const QJsonObject t = s.value("totals").toObject();
    level_->setText(QString::number(t.value("level").toInteger()));
    enemies_->setText(QString::number(t.value("enemies_total").toInteger()));
    clearTime_->setText(duration(t.value("clear_time_total").toInteger()));
    stagesCleared_->setText(QString::number(t.value("stages_cleared").toInteger()));
    created_->setText(when(s.value("created").toInteger()));

    const QJsonArray records = s.value("records").toArray();
    records_->setRowCount(int(records.size()));
    for (int i = 0; i < records.size(); ++i) {
        const QJsonObject r = records[i].toObject();
        auto* name = text(recordName(r));
        name->setToolTip(tr("Offset %1").arg(hex(r.value("offset").toInteger(), 4)));
        records_->setItem(i, 0, name);
        records_->setItem(i, 1, text(rankName(r.value("rank").toInteger())));
        records_->setItem(i, 2, text(QStringLiteral("%1 %").arg(r.value("accuracy").toInteger())));
        records_->setItem(i, 3, number(r.value("enemies").toInteger()));
        records_->setItem(i, 4, text(duration(r.value("clear_time").toInteger())));
        records_->setItem(i, 5, number(r.value("damage").toInteger()));
        records_->setItem(i, 6, number(r.value("clears").toInteger()));
        records_->setItem(i, 7, text(when(r.value("first_set").toInteger())));
        records_->setItem(i, 8, number(r.value("unknown1").toInteger()));
    }

    const QJsonArray bests = s.value("bests").toArray();
    bests_->setRowCount(int(bests.size()));
    for (int i = 0; i < bests.size(); ++i) {
        const QJsonObject b = bests[i].toObject();
        const auto cell = [&](int column, const QString& value, const char* atKey) {
            auto* item = text(value);
            item->setToolTip(tr("First seen in the upload of %1").arg(when(b.value(atKey).toInteger())));
            bests_->setItem(i, column, item);
        };
        bests_->setItem(i, 0, text(recordName(b)));
        cell(1, rankName(b.value("rank").toInteger()), "rank_at");
        cell(2, QStringLiteral("%1 %").arg(b.value("accuracy").toInteger()), "accuracy_at");
        cell(3, QString::number(b.value("enemies").toInteger()), "enemies_at");
        cell(4, duration(b.value("clear_time").toInteger()), "clear_time_at");
        cell(5, QString::number(b.value("damage").toInteger()), "damage_at");
        bests_->setItem(i, 6, number(b.value("clears").toInteger()));
    }

    const QJsonArray changes = s.value("changes").toArray();
    changes_->setRowCount(int(changes.size()));
    for (int i = 0; i < changes.size(); ++i) {
        const QJsonObject c = changes[i].toObject();
        const qint64 before = c.value("before").toInteger();
        const qint64 after = c.value("after").toInteger();
        changes_->setItem(i, 0, text(hex(c.value("offset").toInteger(), 4)));
        changes_->setItem(i, 1, text(c.value("field").toString().isEmpty() ? tr("(not mapped)") : c.value("field").toString()));
        changes_->setItem(i, 2, number(before));
        changes_->setItem(i, 3, number(after));
        QString difference = after >= before ? QStringLiteral("+%1").arg(after - before) : QString::number(after - before);
        if (c.value("field").toString().endsWith(QLatin1String("rank/byte1/accuracy"))) {
            // Three byte fields share this word.
            const auto byte = [](qint64 v, int n) { return (v >> (8 * n)) & 0xff; };
            difference = tr("rank %1 -> %2, byte 1 %3 -> %4, accuracy %5 -> %6")
                             .arg(rankName(byte(before, 0)), rankName(byte(after, 0)))
                             .arg(byte(before, 1)).arg(byte(after, 1))
                             .arg(byte(before, 2)).arg(byte(after, 2));
        }
        changes_->setItem(i, 4, text(difference));
    }
    tabs_->setTabText(2, s.contains("previous_id") ? tr("Changes (%1 since %2)").arg(changes.size()).arg(when(s.value("previous_uploaded").toInteger()))
                                                   : tr("Changes (first upload)"));

    const QJsonArray other = s.value("other").toArray();
    other_->setRowCount(int(other.size()));
    for (int i = 0; i < other.size(); ++i) {
        const QJsonObject o = other[i].toObject();
        const qint64 v = o.value("value").toInteger();
        other_->setItem(i, 0, text(hex(o.value("offset").toInteger(), 4)));
        other_->setItem(i, 1, number(v));
        other_->setItem(i, 2, text(QStringLiteral("%1").arg(v, 8, 16, QLatin1Char('0'))));
    }

    for (QTableWidget* t : {records_, bests_, changes_, other_}) {
        t->resizeColumnsToContents();
    }
}
