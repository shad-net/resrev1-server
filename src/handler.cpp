// SPDX-FileCopyrightText: Copyright 2026 Resident Evil Revelations Custom Server Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "handler.h"

#include <QLoggingCategory>

namespace {
Q_LOGGING_CATEGORY(lcHandler, "handler")
}

void handleRequest(const HttpRequest& request, const QString& endpoint,
                   const HttpServer::Responder& respond) {

    // Add endpoints above. Everything else is answered 404 and marked in the log
    qCWarning(lcHandler).noquote() << "unhandled:" << request.method << endpoint;
    HttpResponse response;
    response.status = 404;
    response.contentType = "text/plain";
    response.body = "Not Found";
    respond(response);
}
