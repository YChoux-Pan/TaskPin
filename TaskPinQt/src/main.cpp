#include "app/AppHost.h"
#include "core/SelfTest.h"
#include "core/Settings.h"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLocale>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>
#include <cstdio>

static void fileLogger(QtMsgType type, const QMessageLogContext &, const QString &msg)
{
    static QFile log(Settings::logPath());
    static bool opened = false;
    if (!opened) {
        QDir().mkpath(QFileInfo(log).absolutePath());
        opened = log.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
    }
    const char *level = type == QtWarningMsg ? "WARN" : type == QtCriticalMsg ? "CRIT" : "INFO";
    const QString line = QStringLiteral("%1 [%2] %3\n")
                             .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"),
                                  QString::fromLatin1(level), msg);
    if (opened) {
        QTextStream ts(&log);
        ts << line;
        ts.flush();
    }
    fprintf(stderr, "%s", qUtf8Printable(line));
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("TaskPinQt"));
    QApplication::setOrganizationName(QStringLiteral("TaskPin"));
    QApplication::setQuitOnLastWindowClosed(false);
    QLocale::setDefault(QLocale(QLocale::Chinese, QLocale::China));
    qInstallMessageHandler(fileLogger);

    const QStringList args = QCoreApplication::arguments();

    // --selftest [dir]: head-less assertions, no window, no tray
    const int selftestIndex = args.indexOf(QStringLiteral("--selftest"));
    if (selftestIndex >= 0) {
        const QString dir = (selftestIndex + 1 < args.size()) ? args.at(selftestIndex + 1)
                                                              : Settings::dataDir() + QStringLiteral("/selftest");
        return runSelfTest(dir);
    }

    const int dumpIndex = args.indexOf(QStringLiteral("--dump"));
    const bool dumpRequested = dumpIndex >= 0;
    const QString dumpPath =
        dumpRequested ? ((dumpIndex + 1 < args.size()) ? args.at(dumpIndex + 1)
                                                       : Settings::dataDir() + QStringLiteral("/dump.json"))
                      : QString();

    // --dump: ask the running instance for a snapshot. Starting a second instance here would steal
    // the command pipe from it, which used to break every later --snip.
    if (dumpRequested) {
        QLocalSocket sock;
        sock.connectToServer(QStringLiteral("TaskPinQt.Cmd"));
        if (sock.waitForConnected(600)) {
            sock.write(QStringLiteral("dump %1").arg(dumpPath).toUtf8());
            sock.flush();
            sock.waitForBytesWritten(600);
            sock.waitForReadyRead(8000);
            const QString reply = QString::fromUtf8(sock.readAll()).trimmed();
            fprintf(stderr, "dump via running instance: %s -> %s\n", qUtf8Printable(reply), qUtf8Printable(dumpPath));
            return QFile::exists(dumpPath) ? 0 : 2;
        }
    }

    // CLI: forward "--snip" / "--full" / "--last" / ... to the running instance
    QString pendingVerb;
    if (argc > 1 && !dumpRequested) {
        QString verb = QString::fromLocal8Bit(argv[1]);
        if (verb.startsWith(QStringLiteral("--"))) verb.remove(0, 2);
        QLocalSocket sock;
        sock.connectToServer(QStringLiteral("TaskPinQt.Cmd"));
        if (sock.waitForConnected(500)) {
            sock.write(verb.toUtf8());
            sock.flush();
            sock.waitForBytesWritten(300);
            return 0;
        }
        pendingVerb = verb;   // nothing running yet: this instance performs it after start
    }

    // single instance: a second normal launch just pings the first one and exits
    if (!dumpRequested) {
        QLocalSocket probe;
        probe.connectToServer(QStringLiteral("TaskPinQt.SingleInstance"));
        if (probe.waitForConnected(300)) {
            probe.write("show\n");
            probe.flush();
            probe.waitForBytesWritten(200);
            return 0;
        }
    }

    static QLocalServer server;
    QLocalServer::removeServer(QStringLiteral("TaskPinQt.SingleInstance"));
    server.listen(QStringLiteral("TaskPinQt.SingleInstance"));

    AppHost host;
    host.start(pendingVerb);

    // --dump with no running instance: write the snapshot from this process and exit
    if (dumpRequested) {
        host.writeDump(dumpPath);
        QTimer::singleShot(0, &app, &QCoreApplication::quit);
    }

    QLocalServer *serverPtr = &server;
    QObject::connect(&server, &QLocalServer::newConnection, [serverPtr] {
        while (auto *sock = serverPtr->nextPendingConnection()) sock->deleteLater();
    });

    return app.exec();
}
