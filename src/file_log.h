// SPDX-FileCopyrightText: Copyright 2026 shadNet Custom Server Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QString>

bool startFileLog(const QString& path, qint64 maxBytes, QString* error);
