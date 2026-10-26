// SPDX-FileCopyrightText: Copyright 2026 Resident Evil Revelations Custom Server Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "main_window.h"

#include "event_editor.h"
#include "event_presets.h"
#include "reward_catalog.h"
#include "reward_dialog.h"
#include "schedule_dialog.h"
#include "stats_dialog.h"
#include "version.h"

#include <QComboBox>
#include <QDateTime>
#include <QFile>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QToolBar>
#include <QVBoxLayout>

namespace {

QString when(qint64 secs) {
    if (secs <= 0) {
        return QStringLiteral("-");
    }
    return QDateTime::fromSecsSinceEpoch(secs).toString(QStringLiteral("yyyy-MM-dd hh:mm"));
}

// 4:33 under an hour, 1:02:03 from an hour on.
QString duration(qint64 seconds) {
    if (seconds < 3600) {
        return QStringLiteral("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QLatin1Char('0'));
    }
    return QStringLiteral("%1:%2:%3")
        .arg(seconds / 3600)
        .arg((seconds / 60) % 60, 2, 10, QLatin1Char('0'))
        .arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

QString rankName(qint64 rank) {
    // 0 = S and 1 = A are confirmed in game; 2 and 3 follow the order (B, C).
    switch (rank) {
    case 0:
        return QStringLiteral("S");
    case 1:
        return QStringLiteral("A");
    case 2:
        return QStringLiteral("B");
    case 3:
        return QStringLiteral("C");
    case 4:
        return QStringLiteral("-");
    default:
        return QString::number(rank);
    }
}

QTableWidget* makeTable(const QStringList& headers) {
    auto* t = new QTableWidget(0, int(headers.size()));
    t->setHorizontalHeaderLabels(headers);
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->setSelectionMode(QAbstractItemView::SingleSelection);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->setAlternatingRowColors(true);
    t->verticalHeader()->hide();
    t->horizontalHeader()->setStretchLastSection(true);
    t->setSortingEnabled(true);
    return t;
}

// Row data is kept as the index into the JSON array so sorting does not lose the mapping.
QTableWidgetItem* cell(const QString& text, int index) {
    auto* item = new QTableWidgetItem(text);
    item->setData(Qt::UserRole, index);
    return item;
}

QTableWidgetItem* numberCell(qint64 value, int index) {
    auto* item = new QTableWidgetItem;
    item->setData(Qt::DisplayRole, value);
    item->setData(Qt::UserRole, index);
    return item;
}

int selectedIndex(const QTableWidget* table) {
    const auto rows = table->selectionModel()->selectedRows();
    if (rows.isEmpty()) {
        return -1;
    }
    return table->item(rows.first().row(), 0)->data(Qt::UserRole).toInt();
}

// What `players` entries are matched against: the player's shadNet online ID.
QString playerTarget(const QJsonObject& p) {
    return p.value("online_id").toString();
}

} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(tr("Revelations Admin %1").arg(Revelations::version()));

    QSettings settings;
    url_ = new QLineEdit(settings.value("url", QStringLiteral("http://127.0.0.1:8081")).toString());
    url_->setPlaceholderText(QStringLiteral("http://127.0.0.1:8081"));
    key_ = new QLineEdit(settings.value("key").toString());
    key_->setEchoMode(QLineEdit::Password);
    key_->setPlaceholderText(tr("admin.key from config.json"));
    connect_ = new QPushButton(tr("Connect"));
    connect_->setDefault(true);
    refresh_ = new QPushButton(tr("Refresh"));
    refresh_->setShortcut(QKeySequence::Refresh);

    auto* bar = new QToolBar(tr("Connection"));
    bar->setMovable(false);
    auto* barWidget = new QWidget;
    auto* barLayout = new QHBoxLayout(barWidget);
    barLayout->setContentsMargins(4, 2, 4, 2);
    barLayout->addWidget(new QLabel(tr("Server")));
    barLayout->addWidget(url_, 3);
    barLayout->addWidget(new QLabel(tr("Key")));
    barLayout->addWidget(key_, 2);
    barLayout->addWidget(connect_);
    barLayout->addWidget(refresh_);
    bar->addWidget(barWidget);
    addToolBar(bar);

    server_ = new QLabel;
    server_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    tabs_ = new QTabWidget;
    tabs_->addTab(buildPlayersTab(), tr("Players"));
    tabs_->addTab(buildRewardsTab(), tr("Rewards"));
    tabs_->addTab(buildEventTab(), tr("Event"));
    tabs_->addTab(buildScheduleTab(), tr("Schedule"));
    tabs_->addTab(buildLeaderboardTab(), tr("Leaderboards"));

    auto* central = new QWidget;
    auto* layout = new QVBoxLayout(central);
    layout->addWidget(server_);
    layout->addWidget(tabs_);
    setCentralWidget(central);

    connect(connect_, &QPushButton::clicked, this, &MainWindow::connectToServer);
    connect(key_, &QLineEdit::returnPressed, this, &MainWindow::connectToServer);
    connect(url_, &QLineEdit::returnPressed, this, &MainWindow::connectToServer);
    connect(refresh_, &QPushButton::clicked, this, &MainWindow::refresh);

    setStatus(tr("Not connected"));
    updateButtons();
    resize(1000, 600);
}

QWidget* MainWindow::buildPlayersTab() {
    playerTable_ = makeTable({tr("ID"), tr("Online ID"), tr("Account"), tr("Code"), tr("Last seen"),
                              tr("First seen"), tr("Upload"), tr("Claimed serials")});
    playerTable_->sortByColumn(4, Qt::DescendingOrder);
    giveReward_ = new QPushButton(tr("Give reward..."));
    resetClaims_ = new QPushButton(tr("Reset claims..."));
    saveUpload_ = new QPushButton(tr("Save upload..."));
    stats_ = new QPushButton(tr("Stats..."));
    stats_->setToolTip(tr("Show the player's stats from their last upload"));
    giveReward_->setToolTip(tr("Add a reward offered only to this player"));
    resetClaims_->setToolTip(tr("Forget claims so the player is offered those rewards again"));
    saveUpload_->setToolTip(tr("Save the player's last uploaded save blob (decrypted)"));

    auto* buttons = new QHBoxLayout;
    buttons->addWidget(giveReward_);
    buttons->addWidget(resetClaims_);
    buttons->addWidget(stats_);
    buttons->addWidget(saveUpload_);
    buttons->addStretch();

    auto* w = new QWidget;
    auto* layout = new QVBoxLayout(w);
    layout->addWidget(playerTable_);
    layout->addLayout(buttons);

    connect(playerTable_, &QTableWidget::itemSelectionChanged, this, &MainWindow::updateButtons);
    connect(giveReward_, &QPushButton::clicked, this,
            [this] { addReward({playerTarget(selectedPlayer())}); });
    connect(resetClaims_, &QPushButton::clicked, this, &MainWindow::resetClaims);
    connect(saveUpload_, &QPushButton::clicked, this, &MainWindow::saveUpload);
    connect(stats_, &QPushButton::clicked, this, &MainWindow::showStats);
    connect(playerTable_, &QTableWidget::cellDoubleClicked, this, [this] {
        if (stats_->isEnabled()) {
            showStats();
        }
    });
    return w;
}

QWidget* MainWindow::buildRewardsTab() {
    rewardTable_ = makeTable({tr("Serial"), tr("Type"), tr("Item"), tr("For"), tr("Available"),
                              tr("Source"), tr("Claims")});
    rewardTable_->sortByColumn(0, Qt::AscendingOrder);
    addReward_ = new QPushButton(tr("Add reward..."));
    deleteReward_ = new QPushButton(tr("Delete"));
    deleteReward_->setToolTip(tr("Rewards from config.json are removed by editing the file"));

    auto* buttons = new QHBoxLayout;
    buttons->addWidget(addReward_);
    buttons->addWidget(deleteReward_);
    buttons->addStretch();

    auto* w = new QWidget;
    auto* layout = new QVBoxLayout(w);
    layout->addWidget(rewardTable_);
    layout->addLayout(buttons);

    connect(rewardTable_, &QTableWidget::itemSelectionChanged, this, &MainWindow::updateButtons);
    connect(addReward_, &QPushButton::clicked, this, [this] { addReward({}); });
    connect(deleteReward_, &QPushButton::clicked, this, &MainWindow::deleteReward);
    return w;
}

QWidget* MainWindow::buildLeaderboardTab() {
    playerBoard_ = makeTable({tr("#"), tr("Player"), tr("Level"), tr("Enemies routed"),
                              tr("Time played"), tr("Raid runs"), tr("Stages cleared")});
    stagePicker_ = new QComboBox;
    stageBoard_ = makeTable({tr("#"), tr("Player"), tr("Clear time"), tr("Rank"),
                             tr("Damage taken"), tr("Accuracy"), tr("Enemies"), tr("Set")});
    boardsNote_ = new QLabel;
    boardsNote_->setWordWrap(true);

    auto* playersBox = new QGroupBox(tr("Players (highest level first)"));
    auto* playersLayout = new QVBoxLayout(playersBox);
    playersLayout->addWidget(playerBoard_);

    auto* stageBox = new QGroupBox(tr("Stage (fastest clear first)"));
    auto* stageLayout = new QVBoxLayout(stageBox);
    stageLayout->addWidget(stagePicker_);
    stageLayout->addWidget(stageBoard_);

    auto* w = new QWidget;
    auto* layout = new QVBoxLayout(w);
    layout->addWidget(boardsNote_);
    layout->addWidget(playersBox, 1);
    layout->addWidget(stageBox, 2);

    connect(stagePicker_, qOverload<int>(&QComboBox::currentIndexChanged), this,
            &MainWindow::showStageBoard);
    return w;
}

void MainWindow::showLeaderboards(const QJsonObject& boards) {
    boards_ = boards;
    const QJsonArray players = boards.value("players").toArray();
    playerBoard_->setSortingEnabled(false);
    playerBoard_->clearContents();
    playerBoard_->setRowCount(int(players.size()));
    for (int i = 0; i < players.size(); ++i) {
        const QJsonObject r = players[i].toObject();
        playerBoard_->setItem(i, 0, numberCell(r.value("place").toInteger(), i));
        playerBoard_->setItem(i, 1, cell(r.value("player").toString(), i));
        playerBoard_->setItem(i, 2, numberCell(r.value("level").toInteger(), i));
        playerBoard_->setItem(i, 3, numberCell(r.value("enemies").toInteger(), i));
        playerBoard_->setItem(i, 4, cell(duration(r.value("time_played").toInteger()), i));
        playerBoard_->setItem(i, 5, numberCell(r.value("runs").toInteger(), i));
        playerBoard_->setItem(i, 6, numberCell(r.value("stages_cleared").toInteger(), i));
    }
    playerBoard_->setSortingEnabled(true);
    playerBoard_->sortByColumn(0, Qt::AscendingOrder);
    playerBoard_->resizeColumnsToContents();

    const QString current = stagePicker_->currentData().toString();
    stagePicker_->blockSignals(true);
    stagePicker_->clear();
    const QJsonArray stages = boards.value("stages").toArray();
    for (int i = 0; i < stages.size(); ++i) {
        const QJsonObject st = stages[i].toObject();
        const QString key =
            st.value("table").toString() + QString::number(st.value("index").toInteger());
        stagePicker_->addItem(
            tr("%1 (%n player(s))", nullptr, int(st.value("entries").toArray().size()))
                .arg(st.value("name").toString()),
            key);
        if (key == current) {
            stagePicker_->setCurrentIndex(i);
        }
    }
    stagePicker_->blockSignals(false);
    boardsNote_->setText(
        stages.isEmpty() ? tr("No stage has been cleared yet.")
                         : tr("Built from every player's kept uploads. Rank, damage and accuracy "
                              "are each player's bests on the stage."));
    showStageBoard();
}

void MainWindow::showStageBoard() {
    const QJsonArray stages = boards_.value("stages").toArray();
    const int index = stagePicker_->currentIndex();
    const QJsonArray rows = index >= 0 && index < stages.size()
                                ? stages[index].toObject().value("entries").toArray()
                                : QJsonArray();
    stageBoard_->setSortingEnabled(false);
    stageBoard_->clearContents();
    stageBoard_->setRowCount(int(rows.size()));
    for (int i = 0; i < rows.size(); ++i) {
        const QJsonObject r = rows[i].toObject();
        stageBoard_->setItem(i, 0, numberCell(r.value("place").toInteger(), i));
        stageBoard_->setItem(i, 1, cell(r.value("player").toString(), i));
        stageBoard_->setItem(i, 2, cell(duration(r.value("clear_time").toInteger()), i));
        stageBoard_->setItem(i, 3, cell(rankName(r.value("rank").toInteger()), i));
        stageBoard_->setItem(i, 4, numberCell(r.value("damage").toInteger(), i));
        stageBoard_->setItem(i, 5,
                             cell(QStringLiteral("%1 %").arg(r.value("accuracy").toInteger()), i));
        stageBoard_->setItem(i, 6, numberCell(r.value("enemies").toInteger(), i));
        stageBoard_->setItem(i, 7, cell(when(r.value("set").toInteger()), i));
    }
    stageBoard_->setSortingEnabled(true);
    stageBoard_->sortByColumn(0, Qt::AscendingOrder);
    stageBoard_->resizeColumnsToContents();
}

QWidget* MainWindow::buildScheduleTab() {
    scheduleTable_ = makeTable({tr("Name"), tr("When"), tr("Effects"), tr("Status"), tr("Source")});
    scheduleTable_->setSortingEnabled(false);
    scheduleNote_ = new QLabel;
    scheduleNote_->setWordWrap(true);
    addEntry_ = new QPushButton(tr("Add..."));
    editEntry_ = new QPushButton(tr("Edit..."));
    deleteEntry_ = new QPushButton(tr("Delete"));
    editEntry_->setToolTip(tr("Entries from config.json are changed by editing the file"));

    auto* buttons = new QHBoxLayout;
    buttons->addWidget(addEntry_);
    buttons->addWidget(editEntry_);
    buttons->addWidget(deleteEntry_);
    buttons->addStretch();

    auto* w = new QWidget;
    auto* layout = new QVBoxLayout(w);
    layout->addWidget(scheduleNote_);
    layout->addWidget(scheduleTable_);
    layout->addLayout(buttons);

    const auto selectedAdmin = [this] {
        const int row = scheduleTable_->currentRow();
        return row >= 0 && row < adminSchedule_.size() &&
               !scheduleTable_->selectedItems().isEmpty();
    };
    const auto update = [this, selectedAdmin] {
        const bool online = api_.configured() && !server_->text().isEmpty();
        addEntry_->setEnabled(online);
        editEntry_->setEnabled(online && selectedAdmin());
        deleteEntry_->setEnabled(online && selectedAdmin());
    };
    connect(scheduleTable_, &QTableWidget::itemSelectionChanged, this, update);
    connect(addEntry_, &QPushButton::clicked, this, [this] { editScheduleEntry(true); });
    connect(editEntry_, &QPushButton::clicked, this, [this] { editScheduleEntry(false); });
    connect(deleteEntry_, &QPushButton::clicked, this, &MainWindow::deleteScheduleEntry);
    connect(scheduleTable_, &QTableWidget::cellDoubleClicked, this,
            [this, selectedAdmin](int, int) {
                if (selectedAdmin()) {
                    editScheduleEntry(false);
                }
            });
    update();
    return w;
}

void MainWindow::showSchedule(const QJsonObject& reply) {
    adminSchedule_ = QJsonArray();
    for (const QJsonValue& v : reply.value("admin").toArray()) {
        QJsonObject e = v.toObject();
        // Keep only what the server reads, so a save sends the entries back unchanged.
        for (const char* key : {"when", "effects", "open_until", "next_start"}) {
            e.remove(QLatin1String(key));
        }
        adminSchedule_.append(e);
    }
    QVector<QPair<QJsonObject, bool>> rows; // entry, from the admin API
    for (const QJsonValue& v : reply.value("admin").toArray()) {
        rows.append({v.toObject(), true});
    }
    for (const QJsonValue& v : reply.value("config").toArray()) {
        rows.append({v.toObject(), false});
    }
    const QString running = reply.value("running").toObject().value("name").toString();
    bool runningShown = false;
    scheduleTable_->clearContents();
    scheduleTable_->setRowCount(int(rows.size()));
    for (int i = 0; i < rows.size(); ++i) {
        const QJsonObject e = rows[i].first;
        QStringList effects;
        for (const QJsonValue& x : e.value("effects").toArray()) {
            effects << x.toString();
        }
        QString status = tr("over");
        if (e.contains("open_until")) {
            // Only the first open entry is sent; a later one waits behind it.
            const bool sent = !runningShown && e.value("name").toString() == running;
            runningShown = runningShown || sent;
            status = sent ? tr("running until %1").arg(when(e.value("open_until").toInteger()))
                          : tr("open, but an earlier entry runs instead");
        } else if (e.contains("next_start")) {
            status = tr("next %1").arg(when(e.value("next_start").toInteger()));
        }
        scheduleTable_->setItem(i, 0, cell(e.value("name").toString(), i));
        scheduleTable_->setItem(i, 1, cell(e.value("when").toString(), i));
        scheduleTable_->setItem(i, 2, cell(effects.join(QStringLiteral("; ")), i));
        scheduleTable_->setItem(i, 3, cell(status, i));
        scheduleTable_->setItem(i, 4, cell(rows[i].second ? tr("admin") : tr("config.json"), i));
    }
    scheduleTable_->resizeColumnsToContents();
    QString note = tr(
        "The server sends the first entry whose time window is open: entries added here first, "
        "then config.json's event_schedule. With none open, the \"event\" in config.json is sent.");
    if (reply.value("overridden").toBool()) {
        note += ' ' + tr("Right now the event set on the Event tab replaces all of them.");
    }
    scheduleNote_->setText(note);
    emit scheduleTable_->itemSelectionChanged();
}

void MainWindow::editScheduleEntry(bool add) {
    const int row = scheduleTable_->currentRow();
    if (!add && (row < 0 || row >= adminSchedule_.size())) {
        return;
    }
    ScheduleDialog dialog(add ? QJsonObject() : adminSchedule_[row].toObject(), this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    QJsonArray entries = adminSchedule_;
    if (add) {
        entries.append(dialog.entry());
    } else {
        entries[row] = dialog.entry();
    }
    saveSchedule(entries, add ? tr("Scheduled event added") : tr("Scheduled event saved"));
}

void MainWindow::deleteScheduleEntry() {
    const int row = scheduleTable_->currentRow();
    if (row < 0 || row >= adminSchedule_.size()) {
        return;
    }
    const QString name = adminSchedule_[row].toObject().value("name").toString();
    if (QMessageBox::question(this, tr("Delete scheduled event"),
                              tr("Delete \"%1\" from the schedule?").arg(name)) !=
        QMessageBox::Yes) {
        return;
    }
    QJsonArray entries = adminSchedule_;
    entries.removeAt(row);
    saveSchedule(entries, tr("Scheduled event deleted"));
}

void MainWindow::saveSchedule(const QJsonArray& entries, const QString& done) {
    api_.put(QStringLiteral("/api/schedule"), QJsonDocument(entries),
             [this, done](const ApiClient::Reply& r) {
                 if (!r.ok()) {
                     reportError(tr("Save schedule"), r);
                     return;
                 }
                 showSchedule(r.json().object());
                 setStatus(done);
                 refresh(); // the Event tab shows what is sent now
             });
}

QWidget* MainWindow::buildEventTab() {
    eventEditor_ = new EventEditor;
    eventSource_ = new QLabel;
    eventSource_->setWordWrap(true);
    applyEvent_ = new QPushButton(tr("Apply event"));
    applyEvent_->setToolTip(
        tr("Send this event to the server; the game picks it up on its next event check"));
    clearEvent_ = new QPushButton(tr("Back to schedule / config.json"));
    clearEvent_->setToolTip(
        tr("Drop the event set here: the schedule and the \"event\" in config.json apply again"));
    revertEvent_ = new QPushButton(tr("Revert"));
    revertEvent_->setToolTip(tr("Discard edits and show the event the server is sending"));

    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(eventEditor_);

    eventPreset_ = new QComboBox;
    eventPreset_->addItem(tr("Fill from a preset..."));
    for (const Presets::Preset& p : Presets::events()) {
        eventPreset_->addItem(Presets::label(p));
    }
    eventPreset_->setToolTip(tr("Fills the form; nothing is sent until you press Apply event"));

    auto* buttons = new QHBoxLayout;
    buttons->addWidget(applyEvent_);
    buttons->addWidget(revertEvent_);
    buttons->addWidget(clearEvent_);
    buttons->addStretch();
    buttons->addWidget(eventPreset_);

    auto* w = new QWidget;
    auto* layout = new QVBoxLayout(w);
    layout->addWidget(eventSource_);
    layout->addWidget(scroll);
    layout->addLayout(buttons);

    connect(eventPreset_, qOverload<int>(&QComboBox::activated), this, [this](int index) {
        if (index > 0) {
            eventEditor_->setEvent(Presets::events()[index - 1].event);
            setStatus(tr("Preset loaded into the form; press Apply event to send it"));
        }
        eventPreset_->setCurrentIndex(0);
    });

    connect(applyEvent_, &QPushButton::clicked, this, &MainWindow::applyEvent);
    connect(clearEvent_, &QPushButton::clicked, this, &MainWindow::clearEvent);
    connect(revertEvent_, &QPushButton::clicked, this, [this] { displayEvent(eventReply_); });
    return w;
}

void MainWindow::displayEvent(const QJsonObject& reply) {
    eventReply_ = reply;
    const QJsonObject event = reply.value("event").toObject();
    eventEditor_->setEvent(event);
    const QString source = reply.value("source").toString();
    const bool admin = source == QLatin1String("admin");
    const QJsonObject info = reply.value("info").toObject();
    const QJsonObject current = info.value("current").toObject();
    QString text;
    if (admin) {
        text = tr(
            "Sending the event set here (it replaces the schedule and the event in config.json).");
    } else if (source == QLatin1String("schedule")) {
        text = tr("Sending the scheduled event \"%1\" until %2. Applying an event here replaces it "
                  "until you go back.")
                   .arg(current.value("name").toString(), when(current.value("end").toInteger()));
    } else {
        text = tr("Sending the event from config.json. Applying an event here replaces it until "
                  "you go back.");
    }
    if (event.isEmpty()) {
        text += ' ' + tr("The event is empty, so no event is running.");
    }
    QStringList effects;
    for (const QJsonValue& v : current.value("description").toArray()) {
        effects << v.toString();
    }
    if (!effects.isEmpty()) {
        text += ' ' + tr("Players see: %1.").arg(effects.join(QStringLiteral("; ")));
    }
    QStringList next;
    for (const QJsonValue& v : info.value("upcoming").toArray()) {
        const QJsonObject o = v.toObject();
        next << tr("%1 (%2 to %3)")
                    .arg(o.value("name").toString(), when(o.value("start").toInteger()),
                         when(o.value("end").toInteger()));
    }
    if (!next.isEmpty()) {
        text += '\n' + tr("Scheduled next: %1").arg(next.join(QStringLiteral(", ")));
    }
    eventSource_->setText(text);
    clearEvent_->setEnabled(admin);
}

void MainWindow::applyEvent() {
    QString why;
    const auto event = eventEditor_->event(&why);
    if (!event) {
        QMessageBox::warning(this, tr("Apply event"), why);
        return;
    }
    api_.put(QStringLiteral("/api/event"), QJsonDocument(*event),
             [this](const ApiClient::Reply& r) {
                 if (!r.ok()) {
                     QMessageBox::warning(this, tr("Apply event"),
                                          tr("The server refused the event:\n%1").arg(r.error));
                     reportError(tr("Apply event"), r);
                     return;
                 }
                 displayEvent(r.json().object());
                 setStatus(tr("Event applied"));
             });
}

void MainWindow::clearEvent() {
    api_.remove(QStringLiteral("/api/event"), [this](const ApiClient::Reply& r) {
        if (!r.ok()) {
            reportError(tr("Clear event"), r);
            return;
        }
        displayEvent(r.json().object());
        setStatus(tr("Back to the event in config.json"));
    });
}

void MainWindow::connectTo(const QString& url, const QString& key) {
    if (!url.isEmpty()) {
        url_->setText(url);
    }
    if (!key.isEmpty()) {
        key_->setText(key);
    }
    connectToServer();
}

void MainWindow::connectToServer() {
    QUrl url = QUrl::fromUserInput(url_->text().trimmed());
    if (!url.isValid() || url.host().isEmpty()) {
        setStatus(tr("Enter the admin address, such as http://127.0.0.1:8081"), true);
        return;
    }
    api_.setServer(url, key_->text());
    QSettings settings;
    settings.setValue("url", url_->text().trimmed());
    settings.setValue("key", key_->text());
    setStatus(tr("Connecting to %1...").arg(url.toString()));
    api_.get(QStringLiteral("/api/status"), [this](const ApiClient::Reply& r) {
        if (!r.ok()) {
            server_->clear();
            players_ = {};
            rewards_ = {};
            showPlayers({});
            showRewards({});
            reportError(tr("Connect"), r);
            updateButtons();
            return;
        }
        const QJsonObject s = r.json().object();
        // Servers from before version reporting send "1.0" without a commit.
        const QString serverVersion = s.value("version").toString();
        const QString serverCommit = s.value("commit").toString();
        QString versions = tr("server %1").arg(serverVersion);
        if (!serverCommit.isEmpty()) {
            versions += QStringLiteral(" (%1)").arg(serverCommit);
        }
        if (serverVersion != Revelations::version()) {
            versions += tr(", this admin app is %1: some features may not match")
                            .arg(Revelations::version());
        }
        server_->setText(tr("Game server %1%2  ·  shadNet %3  ·  up %4 min  ·  %5")
                             .arg(s.value("listen").toString(), s.value("base_path").toString(),
                                  s.value("shadnet_url").toString())
                             .arg(s.value("uptime").toInteger() / 60)
                             .arg(versions));
        server_->setToolTip(
            tr("Server built %1\nAdmin app: %2")
                .arg(s.value("build_timestamp").toString(),
                     Revelations::versionLine(QStringLiteral("revelations-admin"))));
        refresh();
    });
}

void MainWindow::refresh() {
    if (!api_.configured()) {
        return;
    }
    api_.get(QStringLiteral("/api/players"), [this](const ApiClient::Reply& r) {
        if (!r.ok()) {
            reportError(tr("Load players"), r);
            return;
        }
        players_ = r.json().array();
        showPlayers(players_);
        api_.get(QStringLiteral("/api/rewards"), [this](const ApiClient::Reply& r2) {
            if (!r2.ok()) {
                reportError(tr("Load rewards"), r2);
                return;
            }
            rewards_ = r2.json().array();
            showRewards(rewards_);
            api_.get(QStringLiteral("/api/event"), [this](const ApiClient::Reply& r3) {
                if (r3.ok()) {
                    displayEvent(r3.json().object());
                } else {
                    reportError(tr("Load event"), r3);
                }
                api_.get(QStringLiteral("/api/schedule"), [this](const ApiClient::Reply& r5) {
                    if (r5.ok()) {
                        showSchedule(r5.json().object());
                    } else {
                        reportError(tr("Load schedule"), r5);
                    }
                });
                api_.get(QStringLiteral("/api/leaderboards"), [this](const ApiClient::Reply& r4) {
                    if (r4.ok()) {
                        showLeaderboards(r4.json().object());
                    } else {
                        reportError(tr("Load leaderboards"), r4);
                    }
                });
            });
            setStatus(tr("Connected: %n player(s)", nullptr, int(players_.size())) +
                      QStringLiteral(", ") + tr("%n reward(s)", nullptr, int(rewards_.size())));
        });
    });
}

void MainWindow::showPlayers(const QJsonArray& players) {
    playerTable_->setSortingEnabled(false);
    playerTable_->clearContents();
    playerTable_->setRowCount(int(players.size()));
    for (int i = 0; i < players.size(); ++i) {
        const QJsonObject p = players[i].toObject();
        QStringList serials;
        for (const QJsonValue& c : p.value("claims").toArray()) {
            serials << QString::number(c.toObject().value("serial").toInteger());
        }
        const qint64 size = p.value("upload_size").toInteger();
        const QString upload =
            size > 0
                ? tr("%1 (%2 bytes)").arg(when(p.value("upload_updated").toInteger())).arg(size)
                : QStringLiteral("-");
        playerTable_->setItem(i, 0, numberCell(p.value("id").toInteger(), i));
        playerTable_->setItem(i, 1, cell(p.value("online_id").toString(), i));
        playerTable_->setItem(i, 2, cell(p.value("identity").toString(), i));
        playerTable_->setItem(i, 3, cell(p.value("code").toString(), i));
        playerTable_->setItem(i, 4, cell(when(p.value("last_seen").toInteger()), i));
        playerTable_->setItem(i, 5, cell(when(p.value("created").toInteger()), i));
        playerTable_->setItem(i, 6, cell(upload, i));
        playerTable_->setItem(
            i, 7, cell(serials.isEmpty() ? QStringLiteral("-") : serials.join(", "), i));
    }
    playerTable_->setSortingEnabled(true);
    playerTable_->resizeColumnsToContents();
    updateButtons();
}

void MainWindow::showRewards(const QJsonArray& rewards) {
    rewardTable_->setSortingEnabled(false);
    rewardTable_->clearContents();
    rewardTable_->setRowCount(int(rewards.size()));
    for (int i = 0; i < rewards.size(); ++i) {
        const QJsonObject r = rewards[i].toObject();
        QStringList players;
        for (const QJsonValue& p : r.value("players").toArray()) {
            players << p.toString();
        }
        QString source = r.value("source").toString() == QLatin1String("config") ? tr("config.json")
                                                                                 : tr("admin");
        if (r.value("hidden").toBool()) {
            source += tr(" (hidden: serial used in config.json)");
        }
        rewardTable_->setItem(i, 0, numberCell(r.value("serial").toInteger(), i));
        rewardTable_->setItem(i, 1, cell(r.value("type").toString(), i));
        rewardTable_->setItem(i, 2, cell(Catalog::describe(r), i));
        rewardTable_->setItem(i, 3,
                              cell(players.isEmpty() ? tr("everyone") : players.join(", "), i));
        QString available = tr("always");
        if (r.contains("available")) {
            available = r.value("available").toString();
            if (r.value("active").toBool()) {
                available += r.contains("current_serial")
                                 ? tr("  [open, sending serial %1]")
                                       .arg(r.value("current_serial").toInteger())
                                 : tr("  [open]");
            } else if (r.contains("next_start")) {
                available += tr("  [opens %1]").arg(when(r.value("next_start").toInteger()));
            } else {
                available += tr("  [over]");
            }
        }
        rewardTable_->setItem(i, 4, cell(available, i));
        rewardTable_->setItem(i, 5, cell(source, i));
        rewardTable_->setItem(i, 6, numberCell(r.value("claims").toInteger(), i));
    }
    rewardTable_->setSortingEnabled(true);
    rewardTable_->resizeColumnsToContents();
    updateButtons();
}

void MainWindow::updateButtons() {
    const bool online = api_.configured() && !server_->text().isEmpty();
    const QJsonObject player = selectedPlayer();
    const QJsonObject reward = selectedReward();
    refresh_->setEnabled(online);
    addReward_->setEnabled(online);
    giveReward_->setEnabled(online && !playerTarget(player).isEmpty());
    resetClaims_->setEnabled(online && !player.value("claims").toArray().isEmpty());
    saveUpload_->setEnabled(online && player.value("upload_size").toInteger() > 0);
    stats_->setEnabled(saveUpload_->isEnabled());
    deleteReward_->setEnabled(online &&
                              reward.value("source").toString() == QLatin1String("admin"));
    applyEvent_->setEnabled(online);
    revertEvent_->setEnabled(online);
    if (!online) {
        clearEvent_->setEnabled(false);
    }
}

void MainWindow::setStatus(const QString& text, bool error) {
    statusBar()->setStyleSheet(error ? QStringLiteral("color: #c0392b") : QString());
    statusBar()->showMessage(text);
}

void MainWindow::reportError(const QString& action, const ApiClient::Reply& reply) {
    QString message = reply.error;
    if (reply.status == 401) {
        message = tr("the key was refused; use admin.key from the server's config.json");
    }
    setStatus(tr("%1 failed: %2").arg(action, message), true);
}

QJsonObject MainWindow::selectedPlayer() const {
    const int i = selectedIndex(playerTable_);
    return i < 0 ? QJsonObject() : players_[i].toObject();
}

QJsonObject MainWindow::selectedReward() const {
    const int i = selectedIndex(rewardTable_);
    return i < 0 ? QJsonObject() : rewards_[i].toObject();
}

qint64 MainWindow::nextSerial() const {
    qint64 max = 1000;
    for (const QJsonValue& r : rewards_) {
        max = std::max(max, r.toObject().value("serial").toInteger());
    }
    return max + 1;
}

void MainWindow::addReward(const QStringList& players) {
    RewardDialog dialog(nextSerial(), players, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const QJsonObject reward = dialog.reward();
    api_.post(QStringLiteral("/api/rewards"), QJsonDocument(reward),
              [this, reward](const ApiClient::Reply& r) {
                  if (!r.ok()) {
                      QMessageBox::warning(this, tr("Add reward"),
                                           tr("The server refused the reward:\n%1").arg(r.error));
                      reportError(tr("Add reward"), r);
                      return;
                  }
                  setStatus(tr("Added reward %1: %2")
                                .arg(reward.value("serial").toInteger())
                                .arg(Catalog::describe(reward)));
                  refresh();
              });
}

void MainWindow::deleteReward() {
    const QJsonObject reward = selectedReward();
    const qint64 serial = reward.value("serial").toInteger();
    const QString text = tr("Delete reward %1 (%2)?").arg(serial).arg(Catalog::describe(reward));
    QString detail;
    if (reward.value("claims").toInteger() > 0) {
        detail = tr("\n\nPlayers who claimed it keep it, and their claims stay recorded, so "
                    "reusing serial %1 "
                    "later will not offer the new reward to them.")
                     .arg(serial);
    }
    if (QMessageBox::question(this, tr("Delete reward"), text + detail) != QMessageBox::Yes) {
        return;
    }
    api_.remove(QStringLiteral("/api/rewards/%1").arg(serial),
                [this, serial](const ApiClient::Reply& r) {
                    if (!r.ok()) {
                        reportError(tr("Delete reward"), r);
                        return;
                    }
                    setStatus(tr("Deleted reward %1").arg(serial));
                    refresh();
                });
}

void MainWindow::resetClaims() {
    const QJsonObject player = selectedPlayer();
    const QString all = tr("All claims");
    QStringList choices{all};
    for (const QJsonValue& c : player.value("claims").toArray()) {
        const qint64 serial = c.toObject().value("serial").toInteger();
        QString label = QString::number(serial);
        for (const QJsonValue& r : rewards_) {
            if (r.toObject().value("serial").toInteger() == serial) {
                label += QStringLiteral("  ") + Catalog::describe(r.toObject());
            }
        }
        choices << label;
    }
    bool ok = false;
    const QString choice = QInputDialog::getItem(
        this, tr("Reset claims"),
        tr("Player %1 will be offered these rewards again:").arg(playerTarget(player)), choices, 0,
        false, &ok);
    if (!ok) {
        return;
    }
    QJsonObject body;
    if (choice != all) {
        body.insert("serial", choice.section(' ', 0, 0).toLongLong());
    }
    const qint64 id = player.value("id").toInteger();
    api_.post(QStringLiteral("/api/players/%1/reset-claims").arg(id), QJsonDocument(body),
              [this](const ApiClient::Reply& r) {
                  if (!r.ok()) {
                      reportError(tr("Reset claims"), r);
                      return;
                  }
                  setStatus(tr("Reset %n claim(s)", nullptr,
                               int(r.json().object().value("removed").toInteger())));
                  refresh();
              });
}

void MainWindow::showStats() {
    const QJsonObject player = selectedPlayer();
    const qint64 id = player.value("id").toInteger();
    const QString name = player.value("online_id").toString().isEmpty()
                             ? player.value("identity").toString()
                             : player.value("online_id").toString();
    auto* dialog = new StatsDialog(&api_, id, name, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
}

void MainWindow::saveUpload() {
    const QJsonObject player = selectedPlayer();
    const qint64 id = player.value("id").toInteger();
    const QString name = QFileDialog::getSaveFileName(
        this, tr("Save upload"),
        QStringLiteral("upload-%1.bin").arg(playerTarget(player).replace(':', '-')));
    if (name.isEmpty()) {
        return;
    }
    api_.get(QStringLiteral("/api/players/%1/upload").arg(id),
             [this, name](const ApiClient::Reply& r) {
                 if (!r.ok()) {
                     reportError(tr("Save upload"), r);
                     return;
                 }
                 QFile file(name);
                 if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
                     file.write(r.body) != r.body.size()) {
                     setStatus(tr("Cannot write %1: %2").arg(name, file.errorString()), true);
                     return;
                 }
                 setStatus(tr("Saved %1 bytes to %2").arg(r.body.size()).arg(name));
             });
}
