// SPDX-FileCopyrightText: Copyright 2026 Resident Evil Revelations Custom Server Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QString>

namespace Revelations {

QString version();
QString commit();
QString buildTimestamp();
QString versionLine(const QString& program);

} // namespace Revelations
