#include "Broker.h"
#include "RemoteClient.h"
#include <QJsonArray>
#include <QSignalSpy>
#include <QFile>
#include <QDateTime>
#include <QProcess>
#include <QSettings>
#include <QSslKey>
#include <QSslSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>
#ifdef Q_OS_WIN
#include <nlohmann/json.hpp>
#include "Win32RemoteBridge.h"
#endif

namespace {
const QByteArray token(64, 'a');
QString pipeName() { return "tvt-remote-test-" + QUuid::createUuid().toString(QUuid::WithoutBraces); }
class Agent : public QObject {
public:
    QLocalSocket socket; QByteArray buffer; QString id; QList<QJsonObject> keys; bool acknowledge = true;
    Agent(const QString& pipe, const QString& label) {
        connect(&socket, &QLocalSocket::connected, this, [this, label] {
            auto o = Remote::message("register"); o["name"] = label; o["channel"] = "Test";
            o["enabled"] = true; o["mode"] = "bml"; send(o);
        });
        connect(&socket, &QLocalSocket::readyRead, this, [this] {
            buffer += socket.readAll();
            while (buffer.contains('\n')) {
                const auto end = buffer.indexOf('\n'); QJsonObject o;
                Remote::decode(buffer.left(end), o); buffer.remove(0, end + 1);
                if (o["type"] == "registered") id = o["target"].toString();
                if (o["type"] == "key") {
                    keys.append(o);
                    if (acknowledge) { auto r = Remote::message("result"); r["request"] = o["request"]; r["status"] = "dispatched"; send(r); }
                }
            }
        });
        socket.connectToServer(pipe);
    }
    void send(const QJsonObject& o) { socket.write(Remote::encode(o) + '\n'); }
};
class Client : public QObject {
public:
    QWebSocket socket; QList<QJsonObject> messages;
    Client(quint16 port, const QByteArray& secret = token) {
        connect(&socket, &QWebSocket::connected, this, [this, secret] {
            auto o = Remote::message("auth"); o["token"] = QString::fromLatin1(secret); send(o);
        });
        connect(&socket, &QWebSocket::textMessageReceived, this, [this](const QString& text) {
            QJsonObject o; if (Remote::decode(text.toUtf8(), o)) messages.append(o);
        });
        socket.open(QUrl(QString("ws://127.0.0.1:%1").arg(port)));
    }
    void send(const QJsonObject& o) { socket.sendTextMessage(QString::fromUtf8(Remote::encode(o))); }
    void claim(const QString& target) { auto o = Remote::message("claim"); o["target"] = target; send(o); }
    void key(const QString& target, int sequence, const QString& key = "blue") {
        auto o = Remote::message("key"); o["id"] = QString::number(sequence); o["sequence"] = sequence;
        o["target"] = target; o["key"] = key; o["action"] = "press";
        o["expires"] = static_cast<double>(QDateTime::currentMSecsSinceEpoch() + 1500); send(o);
    }
    bool has(const QString& type, const QString& status = {}) const {
        for (const auto& o : messages) if (o["type"] == type && (status.isEmpty() || o["status"] == status)) return true;
        return false;
    }
};
}
class Integration : public QObject {
    Q_OBJECT
    QTemporaryDir m_temporary;
    QSslCertificate m_certificate;
    QSslKey m_key;
private slots:
    void initTestCase() {
        QVERIFY(m_temporary.isValid());
        QCoreApplication::setOrganizationName("TVTRemoteTests");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_temporary.path());
#ifdef REMOTE_TEST_OPENSSL
        QProcess openssl;
        openssl.start(QStringLiteral(REMOTE_TEST_OPENSSL), {"req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1",
            "-subj", "/CN=TVTRemote Integration Test", "-keyout", m_temporary.filePath("key.pem"),
            "-out", m_temporary.filePath("cert.pem")});
        QVERIFY(openssl.waitForFinished(10000)); QCOMPARE(openssl.exitCode(), 0);
        QFile cert(m_temporary.filePath("cert.pem")), key(m_temporary.filePath("key.pem"));
        QVERIFY(cert.open(QIODevice::ReadOnly)); QVERIFY(key.open(QIODevice::ReadOnly));
        m_certificate = QSslCertificate(cert.readAll()); m_key = QSslKey(key.readAll(), QSsl::Rsa);
        QVERIFY(!m_certificate.isNull()); QVERIFY(!m_key.isNull());
#endif
    }
    void routingOwnershipAndDisconnect() {
        Broker b(token); const auto pipe = pipeName(); QVERIFY(b.start(QHostAddress::LocalHost, 0, pipe));
        Agent a(pipe, "TVTest A"), other(pipe, "TVTest B");
        QTRY_VERIFY(!a.id.isEmpty() && !other.id.isEmpty()); QVERIFY(a.id != other.id);
        Client first(b.port()), second(b.port());
        QTRY_VERIFY(first.has("authenticated") && second.has("authenticated"));
        first.claim(a.id); QTRY_VERIFY(first.has("selection", "claimed"));
        second.claim(a.id); QTRY_VERIFY(second.has("selection", "busy"));
        second.key(a.id, 1); QTRY_VERIFY(second.has("result", "not_owner"));
        first.key(a.id, 1); QTRY_COMPARE(a.keys.size(), 1); QTRY_VERIFY(first.has("result", "dispatched"));
        QCOMPARE(other.keys.size(), 0);
        first.key(a.id, 1); QTRY_VERIFY(first.has("result", "invalid_sequence")); QCOMPARE(a.keys.size(), 1);
        first.key(a.id, 2, "OpenDevTools"); QTRY_VERIFY(first.has("result", "invalid_key"));
        first.socket.close(); QTRY_COMPARE(first.socket.state(), QAbstractSocket::UnconnectedState);
        second.claim(a.id); QTRY_VERIFY(second.has("selection", "claimed"));
        a.socket.abort(); QTRY_VERIFY(second.has("selection", "target_gone"));
        second.key(other.id, 2); QTRY_VERIFY(second.has("result", "not_owner")); QCOMPARE(other.keys.size(), 0);
        second.messages.clear(); second.claim(other.id); QTRY_VERIFY(second.has("selection", "claimed"));
        second.key(other.id, 3, "green"); QTRY_COMPARE(other.keys.size(), 1);
        QCOMPARE(other.keys[0]["key"].toString(), QString("green"));
    }
    void authenticationAndMalformedFrames() {
        Broker b(token); const auto pipe = pipeName(); QVERIFY(b.start(QHostAddress::LocalHost, 0, pipe));
        Client bad(b.port(), QByteArray(64, 'b'));
        QSignalSpy disconnected(&bad.socket, &QWebSocket::disconnected);
        QTRY_COMPARE(disconnected.size(), 1); QVERIFY(!bad.has("authenticated"));
        Client good(b.port()); QTRY_VERIFY(good.has("authenticated"));
        good.socket.sendTextMessage("{broken"); QTRY_COMPARE(good.socket.state(), QAbstractSocket::UnconnectedState);
        QLocalSocket rogue; rogue.connectToServer(pipe); QTRY_COMPARE(rogue.state(), QLocalSocket::ConnectedState);
        rogue.write(QByteArray(Remote::MaxMessage + 10, 'x')); rogue.flush();
        QTRY_COMPARE(rogue.state(), QLocalSocket::UnconnectedState);
    }
    void threeDigitCodeAndGlobalLockout() {
        const QByteArray code("007");
        Broker b(code); const auto pipe = pipeName(); QVERIFY(b.start(QHostAddress::LocalHost, 0, pipe));
        Agent a(pipe, "Short-code TVTest"); QTRY_VERIFY(!a.id.isEmpty());
        RemoteClient app;
        QSignalSpy results(&app, &RemoteClient::commandResult);
        app.connectTo(QString("ws://127.0.0.1:%1").arg(b.port()), QString::fromLatin1(code), {});
        QTRY_VERIFY(app.connected()); app.claim(a.id); QTRY_COMPARE(app.selected(), a.id);
        app.press("enter"); QTRY_COMPARE(results.size(), 1); QCOMPARE(results[0][1].toString(), QString("dispatched"));
        app.release();
        for (int i = 0; i < 4; ++i) {
            Client wrong(b.port(), QByteArray("008"));
            QSignalSpy closed(&wrong.socket, &QWebSocket::disconnected);
            QTRY_COMPARE(closed.size(), 1); QVERIFY(!wrong.has("authenticated"));
            QCOMPARE(wrong.socket.closeReason(), QString("Authentication failed"));
        }
        // A legitimate connection does not reset the cross-connection counter.
        Client legitimate(b.port(), code); QTRY_VERIFY(legitimate.has("authenticated"));
        Client fifth(b.port(), QByteArray("008"));
        QSignalSpy closed(&fifth.socket, &QWebSocket::disconnected);
        QTRY_COMPARE(closed.size(), 1);
        QCOMPARE(fifth.socket.closeReason(), QString("Authentication locked; restart server"));
        RemoteClient blocked;
        blocked.connectTo(QString("ws://127.0.0.1:%1").arg(b.port()), QString::fromLatin1(code), {});
        QTRY_VERIFY(blocked.status().contains(QStringLiteral("認証に5回失敗"))); QVERIFY(!blocked.connected());
        // Existing authenticated clients and TVTest remain usable after lockout.
        app.claim(a.id); QTRY_COMPARE(app.selected(), a.id); app.press("blue");
        QTRY_COMPARE(results.size(), 2); QCOMPARE(results[1][1].toString(), QString("dispatched"));
    }
    void fragmentedRegistrationAndTimeout() {
        Broker b(token); const auto pipe = pipeName(); QVERIFY(b.start(QHostAddress::LocalHost, 0, pipe));
        QLocalSocket fragmented; fragmented.connectToServer(pipe); QTRY_COMPARE(fragmented.state(), QLocalSocket::ConnectedState);
        auto registration = Remote::message("register"); registration["name"] = "fragmented";
        const auto bytes = Remote::encode(registration) + '\n';
        fragmented.write(bytes.left(7)); fragmented.flush(); QTest::qWait(20);
        fragmented.write(bytes.mid(7)); fragmented.flush(); QTRY_VERIFY(fragmented.bytesAvailable() > 0);
        QJsonObject registered; QVERIFY(Remote::decode(fragmented.readLine().trimmed(), registered));
        QVERIFY(!registered["target"].toString().isEmpty());
        Agent a(pipe, "No response"); a.acknowledge = false; QTRY_VERIFY(!a.id.isEmpty());
        Client c(b.port()); QTRY_VERIFY(c.has("authenticated")); c.claim(a.id); QTRY_VERIFY(c.has("selection", "claimed"));
        c.key(a.id, 1); QTRY_COMPARE(a.keys.size(), 1); QTRY_VERIFY_WITH_TIMEOUT(c.has("result", "timeout"), 4500);
        QTest::qWait(50); QCOMPARE(a.keys.size(), 1);
    }
    void applicationDoesNotRestoreSelection() {
        Broker b(token); const auto pipe = pipeName(); QVERIFY(b.start(QHostAddress::LocalHost, 0, pipe));
        Agent a(pipe, "TVTest"); QTRY_VERIFY(!a.id.isEmpty());
        RemoteClient c; QSignalSpy results(&c, &RemoteClient::commandResult);
        c.connectTo(QString("ws://127.0.0.1:%1").arg(b.port()), QString::fromLatin1(token), {});
        QTRY_VERIFY(c.connected()); QTRY_COMPARE(c.targets().size(), 1);
        c.claim(a.id); QTRY_COMPARE(c.selected(), a.id); c.press("enter"); QTRY_COMPARE(results.size(), 1);
        QCOMPARE(a.keys.size(), 1); c.setActive(false); QVERIFY(c.selected().isEmpty()); QVERIFY(!c.connected());
        c.press("enter");
        c.connectTo(QString("ws://127.0.0.1:%1").arg(b.port()), QString::fromLatin1(token), {});
        QTRY_VERIFY(c.connected()); QVERIFY(c.selected().isEmpty()); c.press("enter");
        QTest::qWait(50); QCOMPARE(a.keys.size(), 1);
    }
    void acknowledgementsCannotCrossTargets() {
        Broker b(token); const auto pipe = pipeName(); QVERIFY(b.start(QHostAddress::LocalHost, 0, pipe));
        Agent a(pipe, "Target"), wrong(pipe, "Wrong target"); a.acknowledge = false;
        QTRY_VERIFY(!a.id.isEmpty() && !wrong.id.isEmpty());
        Client c(b.port()); QTRY_VERIFY(c.has("authenticated")); c.claim(a.id); QTRY_VERIFY(c.has("selection", "claimed"));
        c.key(a.id, 1); QTRY_COMPARE(a.keys.size(), 1);
        auto forged = Remote::message("result"); forged["request"] = a.keys[0]["request"]; forged["status"] = "dispatched";
        wrong.send(forged); QTest::qWait(50); QVERIFY(!c.has("result"));
        a.send(forged); QTRY_VERIFY(c.has("result", "dispatched"));
    }
    void delayedKeysAreRejected() {
        Broker b(token); const auto pipe = pipeName(); QVERIFY(b.start(QHostAddress::LocalHost, 0, pipe));
        Agent a(pipe, "Target"); QTRY_VERIFY(!a.id.isEmpty());
        Client c(b.port()); QTRY_VERIFY(c.has("authenticated")); c.claim(a.id); QTRY_VERIFY(c.has("selection", "claimed"));
        auto o = Remote::message("key"); o["id"] = "stale"; o["sequence"] = 1;
        o["target"] = a.id; o["key"] = "enter"; o["action"] = "press";
        o["expires"] = static_cast<double>(QDateTime::currentMSecsSinceEpoch() - 1); c.send(o);
        QTRY_VERIFY(c.has("result", "expired")); QCOMPARE(a.keys.size(), 0);
    }
    void lanRequiresTlsAndSingleBroker() {
        Broker insecure(token); QVERIFY(!insecure.start(QHostAddress::AnyIPv4, 0, pipeName()));
        const auto pipe = pipeName(); Broker first(token), second(token);
        QVERIFY(first.start(QHostAddress::LocalHost, 0, pipe));
        QVERIFY(!second.start(QHostAddress::LocalHost, 0, pipe));
    }
    void consoleHelpAndErrors() {
        QProcess process;
        process.start(QStringLiteral(REMOTE_SERVER_PATH), {"--help"});
        QVERIFY(process.waitForFinished(5000)); QCOMPARE(process.exitCode(), 0);
        QVERIFY(process.readAllStandardOutput().contains("--token-file"));
        process.start(QStringLiteral(REMOTE_SERVER_PATH), {"--unknown-option"});
        QVERIFY(process.waitForFinished(5000)); QCOMPARE(process.exitCode(), 1);
        QVERIFY(!process.readAllStandardError().isEmpty());
    }
    void tlsPinning() {
        if (m_certificate.isNull()) QSKIP("OpenSSL executable required to generate ephemeral TLS test certificate");
        QVERIFY(QSslSocket::supportsSsl());
        auto ssl = QSslConfiguration::defaultConfiguration();
        ssl.setLocalCertificate(m_certificate); ssl.setPrivateKey(m_key);
        ssl.setPeerVerifyMode(QSslSocket::VerifyNone); ssl.setProtocol(QSsl::TlsV1_2OrLater);
        const QByteArray code("007");
        Broker b(code, true); QVERIFY(b.start(QHostAddress::LocalHost, 0, pipeName(), ssl));
        RemoteClient valid;
        valid.connectTo(QString("wss://127.0.0.1:%1").arg(b.port()), QString::fromLatin1(code),
            QString::fromLatin1(m_certificate.digest(QCryptographicHash::Sha256).toHex()));
        QTRY_VERIFY_WITH_TIMEOUT(valid.connected(), 6000);
        RemoteClient invalid;
        invalid.connectTo(QString("wss://127.0.0.1:%1").arg(b.port()), QString::fromLatin1(code), QString(64, '0'));
        QTRY_VERIFY_WITH_TIMEOUT(invalid.status().contains(QStringLiteral("TLS")) || invalid.status().contains(QStringLiteral("証明書")), 6000);
        QVERIFY(!invalid.connected());
    }
#ifdef Q_OS_WIN
    void nativeBridgeAndReconnect() {
        const auto pipe = pipeName();
        auto b = std::make_unique<Broker>(token); QVERIFY(b->start(QHostAddress::LocalHost, 0, pipe));
        TvtRemote::Agent agent;
        agent.UpdateState("Native TVTest", "Native channel", true, "bml");
        QVERIFY(agent.Start(pipe.toStdWString()));
        RemoteClient c; QSignalSpy results(&c, &RemoteClient::commandResult);
        auto connectClient = [&] { c.connectTo(QString("ws://127.0.0.1:%1").arg(b->port()), QString::fromLatin1(token), {}); };
        connectClient(); QTRY_VERIFY(c.connected()); QTRY_COMPARE(c.targets().size(), 1);
        const auto id = c.targets()[0].toMap()["id"].toString();
        c.claim(id); QTRY_COMPARE(c.selected(), id); c.press("red");
        TvtRemote::Agent::Command command;
        bool received = false;
        QTRY_VERIFY((received = received || agent.Next(command))); QCOMPARE(QString::fromStdString(command.key), QString("red"));
        agent.Complete(command, true); QTRY_COMPARE(results.size(), 1);
        QCOMPARE(results[0][1].toString(), QString("dispatched"));
        c.disconnectFromServer(); b.reset();
        b = std::make_unique<Broker>(token); QVERIFY(b->start(QHostAddress::LocalHost, 0, pipe));
        connectClient(); QTRY_VERIFY(c.connected()); QTRY_COMPARE_WITH_TIMEOUT(c.targets().size(), 1, 5000);
        const auto newId = c.targets()[0].toMap()["id"].toString(); QVERIFY(newId != id); QVERIFY(c.selected().isEmpty());
        agent.Stop(); QTRY_COMPARE(c.targets().size(), 0);
    }
    void windowBridgeDispatchesOnUiThread() {
        Broker b(token); const auto pipe = pipeName(); QVERIFY(b.start(QHostAddress::LocalHost, 0, pipe));
        TvtRemote::Win32Bridge bridge;
        const auto mainThread = GetCurrentThreadId();
        DWORD dispatchThread = 0; QString pressed;
        QVERIFY(bridge.Start(GetModuleHandleW(nullptr), []() -> nlohmann::json {
            return {{"name", "Window bridge"}, {"channel", "Test"}, {"enabled", true}, {"mode", "bml"}};
        }, [&](const std::string& key) {
            dispatchThread = GetCurrentThreadId(); pressed = QString::fromStdString(key); return true;
        }, pipe.toStdWString()));
        RemoteClient c; QSignalSpy results(&c, &RemoteClient::commandResult);
        c.connectTo(QString("ws://127.0.0.1:%1").arg(b.port()), QString::fromLatin1(token), {});
        QTRY_VERIFY(c.connected()); QTRY_COMPARE(c.targets().size(), 1);
        c.claim(c.targets()[0].toMap()["id"].toString()); QTRY_VERIFY(!c.selected().isEmpty());
        c.press("data"); QTRY_COMPARE(pressed, QString("data")); QCOMPARE(dispatchThread, mainThread);
        QTRY_COMPARE(results.size(), 1);
        bridge.Stop(); QTRY_COMPARE(c.targets().size(), 0);
    }
#endif
};
QTEST_GUILESS_MAIN(Integration)
#include "integration.moc"
