// SPDX-FileCopyrightText: Copyright 2026 Resident Evil Revelations Custom Server Project
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QByteArray>
#include <QJsonObject>

class Storage;

QJsonObject buildLeaderboards(Storage& storage, int size);
QByteArray leaderboardsHtml(const QJsonObject& boards);
QString stageRecordName(const QString& table, int index);
