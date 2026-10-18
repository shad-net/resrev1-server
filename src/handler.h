// SPDX-FileCopyrightText: Copyright 2026 Resident Evil Revelations Custom Server Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "http_server.h"

void handleRequest(const HttpRequest& request, const QString& endpoint,
                   const HttpServer::Responder& respond);
