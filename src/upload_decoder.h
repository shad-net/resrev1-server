// SPDX-FileCopyrightText: Copyright 2026 Resident Evil Revelations Custom Server Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QPair>
#include <QVector>

QJsonObject decodeUpload(const QByteArray& data);
QJsonArray compareUploads(const QByteArray& before, const QByteArray& after);
QJsonArray personalBests(const QVector<QPair<QByteArray, qint64>>& uploads);
