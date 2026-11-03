#include "Broker.h"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QTextStream>

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    app.setApplicationName("tvt-remote-server"); app.setApplicationVersion("0.2.0");
    QCommandLineParser parser; parser.addHelpOption(); parser.addVersionOption();
    parser.addOptions({{{"a", "address"}, "Listen IP", "address", "0.0.0.0"},
        {{"p", "port"}, "WebSocket port", "port", "18765"},
        {"pipe", "Local IPC name", "name", Remote::PipeName}});
    if (!parser.parse(app.arguments())) { QTextStream(stderr) << parser.errorText() << '\n'; return 1; }
    if (parser.isSet("help") || parser.isSet("help-all")) { QTextStream(stdout) << parser.helpText(); return 0; }
    if (parser.isSet("version")) { QTextStream(stdout) << app.applicationVersion() << '\n'; return 0; }
    auto fail = [](const QString& error) { QTextStream(stderr) << error << '\n'; return 1; };
    QHostAddress address;
    bool ok = false; const auto port = parser.value("port").toUInt(&ok);
    if (!address.setAddress(parser.value("address")) || !ok || port > 65535) return fail("Invalid address/port");
    Broker broker;
    if (!broker.start(address, quint16(port), parser.value("pipe"))) return fail(broker.errorString());
    QTextStream(stdout) << "WS " << address.toString() << ':' << broker.port()
        << " | IPC " << parser.value("pipe") << Qt::endl;
    return app.exec();
}
