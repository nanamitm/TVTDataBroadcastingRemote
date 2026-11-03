#include "Protocol.h"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QLocalSocket>
#include <QTextStream>
#include <QTimer>

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QCommandLineParser p; p.addHelpOption();
    p.addOptions({{"name", "Instance label", "name", "Demo TVTest"},
        {"channel", "Channel label", "channel", "Demo channel"},
        {"pipe", "Local IPC name", "name", Remote::PipeName}});
    if (!p.parse(app.arguments())) { QTextStream(stderr) << p.errorText() << '\n'; return 1; }
    if (p.isSet("help")) { QTextStream(stdout) << p.helpText(); return 0; }
    QLocalSocket socket; QTimer retry, heartbeat; QByteArray buffer;
    retry.setInterval(2000); heartbeat.setInterval(5000);
    auto send = [&](const QJsonObject& o) { if (socket.state() == QLocalSocket::ConnectedState) socket.write(Remote::encode(o) + '\n'); };
    QObject::connect(&socket, &QLocalSocket::connected, &app, [&] {
        buffer.clear();
        auto o = Remote::message("register"); o["name"] = p.value("name"); o["channel"] = p.value("channel");
        o["pid"] = int(QCoreApplication::applicationPid()); o["enabled"] = true; o["mode"] = "demo"; send(o);
    });
    QObject::connect(&socket, &QLocalSocket::readyRead, &app, [&] {
        buffer += socket.readAll();
        while (buffer.contains('\n')) {
            const auto end = buffer.indexOf('\n'); QJsonObject o;
            const auto line = buffer.left(end); buffer.remove(0, end + 1);
            if (!Remote::decode(line, o)) { socket.abort(); return; }
            if (o["type"] == "key") {
                QTextStream(stdout) << p.value("name") << " key=" << o["key"].toString() << Qt::endl;
                auto result = Remote::message("result"); result["request"] = o["request"]; result["status"] = "dispatched"; send(result);
            }
        }
        if (buffer.size() > Remote::MaxMessage) socket.abort();
    });
    QObject::connect(&heartbeat, &QTimer::timeout, &app, [&] { send(Remote::message("heartbeat")); });
    QObject::connect(&retry, &QTimer::timeout, &app, [&] {
        if (socket.state() == QLocalSocket::UnconnectedState) socket.connectToServer(p.value("pipe"));
    });
    retry.start(); heartbeat.start(); socket.connectToServer(p.value("pipe"));
    return app.exec();
}
