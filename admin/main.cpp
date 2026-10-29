// SPDX-FileCopyrightText: Copyright 2026 Resident Evil Revelations Custom Server Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "main_window.h"
#include "version.h"

#include <QApplication>
#include <QCommandLineParser>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("revelations-server"));
    QApplication::setApplicationName(QStringLiteral("revelations-admin"));
    QApplication::setApplicationVersion(Revelations::Version());

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Admin client for revelations-server."));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption urlOption({"u", "url"},
                                       "Admin API address, such as http://127.0.0.1:8081.", "url");
    const QCommandLineOption keyOption({"k", "key"},
                                       "Admin key (admin.key in the server's config.json).", "key");
    parser.addOptions({urlOption, keyOption});
    parser.process(app);

    MainWindow window;
    window.show();
    if (parser.isSet(urlOption) || parser.isSet(keyOption)) {
        window.connectTo(parser.value(urlOption), parser.value(keyOption));
    }
    return app.exec();
}
