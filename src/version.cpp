// SPDX-FileCopyrightText: Copyright 2026 Resident Evil Revelations Custom Server Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "version.h"

#include "build_info.h"

namespace Revelations {

QString version() {
    return QStringLiteral(REVELATIONS_VERSION);
}

QString commit() {
    return QStringLiteral(REVELATIONS_GIT_COMMIT);
}

QString buildTimestamp() {
    return QStringLiteral(REVELATIONS_BUILD_TIMESTAMP);
}

QString versionLine(const QString& program) {
    const QString built = QStringLiteral("built %1").arg(buildTimestamp());
    return QStringLiteral("%1 %2 (%3)")
        .arg(program, version(),
             commit().isEmpty() ? built : commit() + QStringLiteral(", ") + built);
}

} // namespace Revelations
