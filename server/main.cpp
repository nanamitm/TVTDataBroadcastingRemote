#include "Broker.h"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFile>
#include <QRandomGenerator>
#include <QSslCertificate>
#include <QSslKey>
#include <QTextStream>

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    app.setApplicationName("tvt-remote-server"); app.setApplicationVersion("0.1.0");
    QCommandLineParser parser; parser.addHelpOption(); parser.addVersionOption();
    parser.addOptions({{{"a", "address"}, "Listen IP; LAN requires TLS", "address", "127.0.0.1"},
        {{"p", "port"}, "WebSocket port", "port", "18765"},
        {"pipe", "Local IPC name", "name", Remote::PipeName},
        {"token-file", "Token file (created if missing)", "file"},
        {"cert", "TLS certificate PEM", "file"}, {"key", "TLS private key PEM", "file"}});
    parser.process(app);
    auto fail = [](const QString& error) { QTextStream(stderr) << error << '\n'; return 1; };
    const auto tokenPath = parser.value("token-file");
    if (tokenPath.isEmpty()) return fail("--token-file is required; token contents are never logged");
    QFile tokenFile(tokenPath);
    if (!tokenFile.exists()) {
        if (!tokenFile.open(QIODevice::WriteOnly | QIODevice::NewOnly)) return fail("Cannot create token file: " + tokenFile.errorString());
        tokenFile.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
        QByteArray bytes;
        for (int i = 0; i < 8; ++i) { const auto n = QRandomGenerator::system()->generate(); bytes.append(reinterpret_cast<const char*>(&n), sizeof(n)); }
        const auto token = bytes.toHex();
        if (tokenFile.write(token + '\n') != token.size() + 1) return fail("Cannot write token file");
        tokenFile.close();
    }
    if (!tokenFile.open(QIODevice::ReadOnly)) return fail("Cannot read token file: " + tokenFile.errorString());
    const auto token = tokenFile.read(4097).trimmed();
    if (token.size() < 32 || token.size() > 4096) return fail("Invalid token file length");
    const bool tls = parser.isSet("cert") || parser.isSet("key");
    QSslConfiguration ssl = QSslConfiguration::defaultConfiguration();
    if (tls) {
        QFile certFile(parser.value("cert")), keyFile(parser.value("key"));
        if (!certFile.open(QIODevice::ReadOnly) || !keyFile.open(QIODevice::ReadOnly)) return fail("Cannot read TLS certificate/key");
        const QSslCertificate cert(certFile.readAll(), QSsl::Pem);
        const auto keyData = keyFile.readAll();
        QSslKey key(keyData, QSsl::Rsa); if (key.isNull()) key = QSslKey(keyData, QSsl::Ec);
        if (cert.isNull() || key.isNull()) return fail("Invalid TLS certificate/key");
        ssl.setLocalCertificate(cert); ssl.setPrivateKey(key); ssl.setPeerVerifyMode(QSslSocket::VerifyNone);
        ssl.setProtocol(QSsl::TlsV1_2OrLater);
    }
    QHostAddress address;
    bool ok = false; const auto port = parser.value("port").toUInt(&ok);
    if (!address.setAddress(parser.value("address")) || !ok || port > 65535) return fail("Invalid address/port");
    Broker broker(token, tls);
    if (!broker.start(address, quint16(port), parser.value("pipe"), ssl)) return fail(broker.errorString());
    QTextStream(stdout) << (tls ? "WSS " : "WS ") << address.toString() << ':' << broker.port()
        << " | IPC " << parser.value("pipe") << " | token file: " << tokenPath << Qt::endl;
    return app.exec();
}
