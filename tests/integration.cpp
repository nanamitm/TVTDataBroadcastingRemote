#include "Broker.h"
#include "RemoteClient.h"
#include "StartupSettings.h"
#include "ServerSettings.h"
#include <QJsonArray>
#include <QSignalSpy>
#include <QFile>
#include <QDateTime>
#include <QProcess>
#include <QSettings>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTcpServer>
#include <QTest>
#include <QUuid>
#ifdef Q_OS_WIN
#include <nlohmann/json.hpp>
#include "Win32RemoteBridge.h"
#include "TargetIdentity.h"
#include <shellapi.h>
#endif

namespace {
QString pipeName() { return "tvt-remote-test-" + QUuid::createUuid().toString(QUuid::WithoutBraces); }
class Agent : public QObject {
public:
    QLocalSocket socket; QByteArray buffer; QString id; QList<QJsonObject> keys, controls; bool acknowledge = true;
    QJsonArray channelPages; int channelRequests = 0; // pages sent for each "channels" request
    Agent(const QString& pipe, const QString& label, const QString& token = {}, const QJsonValue& advertised = QJsonValue(QJsonValue::Undefined)) {
        connect(&socket, &QLocalSocket::connected, this, [this, label, token, advertised] {
            auto o = Remote::message("register"); o["name"] = label; o["channel"] = "Test";
            o["enabled"] = true; o["mode"] = "bml"; o["targetToken"] = token;
            if (!advertised.isUndefined()) o["controls"] = advertised;
            send(o);
        });
        connect(&socket, &QLocalSocket::readyRead, this, [this] {
            buffer += socket.readAll();
            while (buffer.contains('\n')) {
                const auto end = buffer.indexOf('\n'); QJsonObject o;
                Remote::decode(buffer.left(end), o); buffer.remove(0, end + 1);
                if (o["type"] == "registered") id = o["target"].toString();
                if (o["type"] == "key" || o["type"] == "control") {
                    (o["type"] == "key" ? keys : controls).append(o);
                    if (acknowledge) { auto r = Remote::message("result"); r["request"] = o["request"]; r["status"] = "dispatched"; send(r); }
                }
                if (o["type"] == "channels") {
                    ++channelRequests;
                    for (const auto& value : std::as_const(channelPages)) {
                        auto page = value.toObject(); page["v"] = 1; page["type"] = "channelPage"; page["request"] = o["request"]; send(page);
                    }
                    auto r = Remote::message("result"); r["request"] = o["request"]; r["status"] = "dispatched"; send(r);
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
    Client(quint16 port) {
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
private slots:
    void initTestCase() {
        QVERIFY(m_temporary.isValid());
        QCoreApplication::setOrganizationName("TVTRemoteTests");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_temporary.path());
    }
    void init() { QSettings().clear(); }
    void lanClientConnectsWithoutCredentials() {
        Broker b; const auto pipe = pipeName(); QVERIFY(b.start(QHostAddress::AnyIPv4, 0, pipe));
        Agent a(pipe, "LAN TVTest"); QTRY_VERIFY(!a.id.isEmpty());
        // This client sends no application message before receiving the list.
        Client passive(b.port()); QTRY_VERIFY(passive.has("ready")); QTRY_VERIFY(passive.has("targets"));
        RemoteClient app; QSignalSpy results(&app, &RemoteClient::commandResult);
        app.connectTo(QString("ws://127.0.0.1:%1").arg(b.port()));
        QTRY_VERIFY(app.connected()); QTRY_COMPARE(app.targets().size(), 1);
        app.claim(a.id); QTRY_COMPARE(app.selected(), a.id); app.press("enter");
        QTRY_COMPARE(results.size(), 1); QCOMPARE(results[0][1].toString(), QString("dispatched"));
    }
    void controlsAreAdvertisedValidatedAndRouted() {
        Broker b; const auto pipe = pipeName(); QVERIFY(b.start(QHostAddress::LocalHost, 0, pipe));
        Agent modern(pipe, "Modern", "modern", QJsonObject{{"caption", false}, {"volume", 40}, {"mute", false},
            {"future", "ignored"}, {"bogus", 500}});
        Agent legacy(pipe, "Legacy", "legacy");
        QTRY_VERIFY(!modern.id.isEmpty()); QTRY_VERIFY(!legacy.id.isEmpty());
        RemoteClient app; QSignalSpy results(&app, &RemoteClient::commandResult);
        app.connectTo(QString("ws://127.0.0.1:%1").arg(b.port()));
        QTRY_VERIFY(app.connected()); QTRY_COMPARE(app.targets().size(), 2);
        app.claim(modern.id); QTRY_COMPARE(app.selected(), modern.id);
        // Unknown names and out-of-range values are dropped by the broker.
        QCOMPARE(app.controls().keys(), QStringList({"caption", "mute", "volume"}));
        app.control("caption", true); app.control("mute", true);
        QTRY_COMPARE(modern.controls.size(), 2);
        QCOMPARE(modern.controls[0]["name"].toString(), QString("caption")); QCOMPARE(modern.controls[0]["value"], QJsonValue(true));
        QTRY_COMPARE(results.size(), 2); QCOMPARE(results[1][1].toString(), QString("dispatched"));
        // A slider drag is coalesced: the first and last values are delivered.
        for (int v = 41; v <= 60; ++v) app.control("volume", v);
        QTRY_COMPARE(modern.controls.size(), 4);
        QCOMPARE(modern.controls[2]["value"].toInt(), 41); QCOMPARE(modern.controls[3]["value"].toInt(), 60);
        QTest::qWait(250); QCOMPARE(modern.controls.size(), 4);
        // Agent state updates are reflected in the selected target's controls.
        auto state = Remote::message("state"); state["name"] = "Modern"; state["targetToken"] = "modern";
        state["controls"] = QJsonObject{{"caption", true}, {"volume", 60}, {"mute", true}}; modern.send(state);
        QTRY_COMPARE(app.controls().value("volume").toInt(), 60); QCOMPARE(app.controls().value("mute").toBool(), true);
        app.stepVolume(-5); QTRY_COMPARE(modern.controls.size(), 5); QCOMPARE(modern.controls[4]["value"].toInt(), 55);
        // Hardware volume keys step from the last request; fast auto-repeats are paced.
        QSignalSpy shown(&app, &RemoteClient::volumeKeyPressed);
        QTest::qWait(200);
        QVERIFY(app.volumeKey(Qt::Key_VolumeUp, false));
        QTRY_COMPARE(modern.controls.size(), 6); QCOMPARE(modern.controls[5]["value"].toInt(), 60);
        QVERIFY(app.volumeKey(Qt::Key_VolumeUp, true)); // within 150 ms: shown, not stepped
        QTest::qWait(250); QCOMPARE(modern.controls.size(), 6); QCOMPARE(shown.size(), 2);
        QVERIFY(app.volumeKey(Qt::Key_VolumeMute, false));
        QTRY_COMPARE(modern.controls.size(), 7); QCOMPARE(modern.controls[6]["name"].toString(), QString("mute"));
        QCOMPARE(modern.controls[6]["value"], QJsonValue(false)); // state reported mute=true
        QVERIFY(!app.volumeKey(Qt::Key_A, false));
        // Legacy agents never receive control messages (they would disconnect on them).
        Client raw(b.port()); QTRY_VERIFY(raw.has("targets")); raw.claim(legacy.id); QTRY_VERIFY(raw.has("selection", "claimed"));
        auto control = Remote::message("control"); control["id"] = "c1"; control["sequence"] = 1; control["target"] = legacy.id;
        control["name"] = "volume"; control["value"] = 10;
        control["expires"] = static_cast<double>(QDateTime::currentMSecsSinceEpoch() + 1500); raw.send(control);
        QTRY_VERIFY(raw.has("result", "unsupported"));
        control["id"] = "c2"; control["sequence"] = 2; control["value"] = 101; raw.send(control);
        QTRY_VERIFY(raw.has("result", "invalid_control"));
        control["id"] = "c3"; control["sequence"] = 3; control["name"] = "caption"; control["value"] = 1; raw.send(control);
        QTRY_COMPARE([&] { int n = 0; for (const auto& o : raw.messages) n += o["status"] == "invalid_control"; return n; }(), 2);
        QCOMPARE(legacy.controls.size(), 0); QCOMPARE(legacy.socket.state(), QLocalSocket::ConnectedState);
        // A malformed controls field disconnects the agent like other invalid metadata.
        Agent malformed(pipe, "Malformed", "malformed", QJsonValue(5));
        QTRY_COMPARE(malformed.socket.state(), QLocalSocket::UnconnectedState);
    }
    void channelListIsPagedAndChannelsAreSelected() {
        Broker b; const auto pipe = pipeName(); QVERIFY(b.start(QHostAddress::LocalHost, 0, pipe));
        auto channel = [](int space, int index, int sid, const QString& name, bool program) {
            QJsonObject c{{"space", space}, {"index", index}, {"remoteControlKey", index + 1}, {"name", name},
                {"serviceId", sid}, {"networkId", 32736}, {"transportStreamId", 32736}};
            if (program) c["program"] = QJsonObject{{"name", "News"}, {"start", 1790000000000.0}, {"duration", 1800}};
            return c;
        };
        const QJsonObject current{{"space", 0}, {"index", -1}, {"serviceId", 1032}, {"networkId", 32736}, {"transportStreamId", 32736}};
        Agent modern(pipe, "Modern", "modern", QJsonObject{{"volume", 10}, {"channel", current}});
        modern.channelPages = {
            QJsonObject{{"space", 0}, {"order", 0}, {"spaceName", "UHF"}, {"channels", QJsonArray{channel(0, 0, 1024, "General", true)}}},
            QJsonObject{{"space", 0}, {"order", 0}, {"spaceName", "UHF"}, {"channels", QJsonArray{channel(0, 1, 1032, "Education", false)}}},
            QJsonObject{{"space", 2}, {"order", 1}, {"spaceName", "BS"}, {"channels", QJsonArray{channel(2, 0, 101, "BS1", true)}}}};
        Agent legacy(pipe, "Legacy", "legacy", QJsonObject{{"volume", 10}});
        QTRY_VERIFY(!modern.id.isEmpty()); QTRY_VERIFY(!legacy.id.isEmpty());
        RemoteClient app; QSignalSpy results(&app, &RemoteClient::commandResult);
        app.connectTo(QString("ws://127.0.0.1:%1").arg(b.port()));
        QTRY_VERIFY(app.connected()); QTRY_COMPARE(app.targets().size(), 2);
        app.claim(modern.id); QTRY_COMPARE(app.selected(), modern.id);
        QCOMPARE(app.controls().value("channel").toMap().value("serviceId").toInt(), 1032);
        // Pages of the same space are merged in order; spaces keep their order.
        app.requestChannels(); QVERIFY(app.channelsLoading());
        QTRY_VERIFY(!app.channelsLoading());
        QCOMPARE(modern.channelRequests, 1);
        const auto spaces = app.channelSpaces(); QCOMPARE(spaces.size(), 2);
        QCOMPARE(spaces[0].toMap().value("name").toString(), QString("UHF"));
        const auto uhf = spaces[0].toMap().value("channels").toList(); QCOMPARE(uhf.size(), 2);
        QCOMPARE(uhf[0].toMap().value("program").toMap().value("name").toString(), QString("News"));
        QCOMPARE(spaces[1].toMap().value("space").toInt(), 2);
        // Selecting sends only the identifying fields as a "channel" control.
        app.selectChannel(uhf[0].toMap()); QTRY_COMPARE(modern.controls.size(), 1);
        const auto value = modern.controls[0]["value"].toObject();
        QCOMPARE(modern.controls[0]["name"].toString(), QString("channel"));
        QCOMPARE(value.size(), 5); QCOMPARE(value["serviceId"].toInt(), 1024); QCOMPARE(value["index"].toInt(), 0);
        // Requests are validated, and agents without "channel" never get them.
        Client raw(b.port()); QTRY_VERIFY(raw.has("targets")); raw.claim(legacy.id); QTRY_VERIFY(raw.has("selection", "claimed"));
        auto request = Remote::message("channels"); request["id"] = "l1"; request["sequence"] = 1; request["target"] = legacy.id;
        request["expires"] = static_cast<double>(QDateTime::currentMSecsSinceEpoch() + 1500); raw.send(request);
        QTRY_VERIFY(raw.has("result", "unsupported")); QCOMPARE(legacy.channelRequests, 0);
        auto bad = Remote::message("control"); bad["id"] = "c1"; bad["sequence"] = 2; bad["target"] = legacy.id; bad["name"] = "channel";
        bad["value"] = QJsonObject{{"space", -1}, {"index", 0}, {"serviceId", 1}, {"networkId", 1}, {"transportStreamId", 1}};
        bad["expires"] = static_cast<double>(QDateTime::currentMSecsSinceEpoch() + 1500); raw.send(bad);
        QTRY_VERIFY(raw.has("result", "invalid_control"));
        // Pages for a request owned by another client are not relayed to others.
        QCOMPARE([&] { int n = 0; for (const auto& o : raw.messages) n += o["type"] == "channelPage"; return n; }(), 0);
    }
    void stableIdentitySurvivesPluginRestartAndColdStart() {
        Broker b; const auto pipe = pipeName(); QVERIFY(b.start(QHostAddress::LocalHost, 0, pipe));
        Agent a(pipe, "First PID", "living-room"); QTRY_VERIFY(!a.id.isEmpty());
        QString replacement;
        {
            RemoteClient app; app.connectTo(QString("ws://127.0.0.1:%1").arg(b.port()));
            QTRY_COMPARE(app.targets().size(), 1); app.claim(a.id); QTRY_COMPARE(app.selected(), a.id);
            QCOMPARE(QSettings().value("targetToken").toString(), QString("living-room"));
            a.socket.abort(); QTRY_COMPARE(app.targets().size(), 0); QVERIFY(app.selected().isEmpty());
            Agent wrong(pipe, "Same label", "bedroom"); QTRY_VERIFY(!wrong.id.isEmpty());
            QTRY_COMPARE(app.targets().size(), 1); QVERIFY(app.selected().isEmpty());
            Agent restarted(pipe, "Different PID", "living-room"); QTRY_VERIFY(!restarted.id.isEmpty());
            replacement = restarted.id; QVERIFY(replacement != a.id);
            QTRY_COMPARE(app.selected(), replacement); QCOMPARE(restarted.keys.size(), 0);
            app.press("enter"); QTRY_COMPARE(restarted.keys.size(), 1); QCOMPARE(wrong.keys.size(), 0);
            app.setActive(false); QTRY_VERIFY(!app.connected());
        }
        Agent next(pipe, "Third PID", "living-room"); QTRY_VERIFY(!next.id.isEmpty());
        RemoteClient cold; cold.start(); QTRY_COMPARE(cold.selected(), next.id); QCOMPARE(next.keys.size(), 0);
    }
    void duplicateIdentityBlocksAutomaticSelectionAndClaimRace() {
        Broker b; const auto pipe = pipeName(); QVERIFY(b.start(QHostAddress::LocalHost, 0, pipe));
        Agent a(pipe, "First", "shared"), duplicate(pipe, "Second", "shared");
        QTRY_VERIFY(!a.id.isEmpty() && !duplicate.id.isEmpty());
        const auto url = QString("ws://127.0.0.1:%1").arg(b.port());
        QSettings settings; settings.setValue("url", url); settings.setValue("targetUrl", url);
        settings.setValue("targetId", "old-session"); settings.setValue("targetToken", "shared");
        RemoteClient app; app.start(); QTRY_COMPARE(app.targets().size(), 2);
        QVERIFY(app.selected().isEmpty()); QVERIFY(app.status().contains(QStringLiteral("重複")));
        QVERIFY(app.targets()[0].toMap()["identityConflict"].toBool());
        Client stale(b.port()); QTRY_VERIFY(stale.has("ready"));
        auto claim = Remote::message("claim"); claim["target"] = a.id; claim["targetToken"] = "shared";
        stale.send(claim); QTRY_VERIFY(stale.has("selection", "identity_conflict"));
        duplicate.socket.abort(); QTRY_COMPARE(app.selected(), a.id); QCOMPARE(a.keys.size(), 0);
    }
    void stableIdentityRespectsOtherOwner() {
        Broker b; const auto pipe = pipeName(); QVERIFY(b.start(QHostAddress::LocalHost, 0, pipe));
        Agent a(pipe, "TVTest", "living"); QTRY_VERIFY(!a.id.isEmpty());
        Client owner(b.port()); QTRY_VERIFY(owner.has("ready")); owner.claim(a.id); QTRY_VERIFY(owner.has("selection", "claimed"));
        const auto url = QString("ws://127.0.0.1:%1").arg(b.port());
        QSettings settings; settings.setValue("url", url); settings.setValue("targetUrl", url);
        settings.setValue("targetId", "previous-id"); settings.setValue("targetToken", "living");
        RemoteClient app; app.start(); QTRY_COMPARE(app.targets().size(), 1); QVERIFY(app.selected().isEmpty());
        QVERIFY(app.status().contains(QStringLiteral("操作中")));
    }
    void invalidIdentityIsNotTruncatedIntoAnotherIdentity() {
        Broker b; const auto pipe = pipeName(); QVERIFY(b.start(QHostAddress::LocalHost, 0, pipe));
        QVERIFY(Remote::validTargetToken("Room-1_2.3")); QVERIFY(!Remote::validTargetToken("room 1"));
        Agent invalid(pipe, "Bad", QString(65, 'a')); QTRY_COMPARE(invalid.socket.state(), QLocalSocket::UnconnectedState);
        Client client(b.port()); QTRY_VERIFY(client.has("targets"));
        for (const auto& o : client.messages) if (o["type"] == "targets") QCOMPARE(o["targets"].toArray().size(), 0);
    }
    void migratesPreviousConnectionSettings() {
        QSettings settings; settings.setValue("url", "wss://192.0.2.10:18765");
        settings.setValue("fingerprint", QString(64, 'a')); settings.sync();
        RemoteClient app;
        QCOMPARE(app.savedUrl(), QString("ws://192.0.2.10:18765"));
        QVERIFY(!QSettings().contains("fingerprint"));
    }
    void routingOwnershipAndDisconnect() {
        Broker b; const auto pipe = pipeName(); QVERIFY(b.start(QHostAddress::LocalHost, 0, pipe));
        Agent a(pipe, "TVTest A"), other(pipe, "TVTest B");
        QTRY_VERIFY(!a.id.isEmpty() && !other.id.isEmpty()); QVERIFY(a.id != other.id);
        Client first(b.port()), second(b.port());
        QTRY_VERIFY(first.has("ready") && second.has("ready"));
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
    void malformedFramesAreRejected() {
        Broker b; const auto pipe = pipeName(); QVERIFY(b.start(QHostAddress::LocalHost, 0, pipe));
        Client good(b.port()); QTRY_VERIFY(good.has("ready"));
        good.socket.sendTextMessage("{broken"); QTRY_COMPARE(good.socket.state(), QAbstractSocket::UnconnectedState);
        QLocalSocket rogue; rogue.connectToServer(pipe); QTRY_COMPARE(rogue.state(), QLocalSocket::ConnectedState);
        rogue.write(QByteArray(Remote::MaxMessage + 10, 'x')); rogue.flush();
        QTRY_COMPARE(rogue.state(), QLocalSocket::UnconnectedState);
    }
    void fragmentedRegistrationAndTimeout() {
        Broker b; const auto pipe = pipeName(); QVERIFY(b.start(QHostAddress::LocalHost, 0, pipe));
        QLocalSocket fragmented; fragmented.connectToServer(pipe); QTRY_COMPARE(fragmented.state(), QLocalSocket::ConnectedState);
        auto registration = Remote::message("register"); registration["name"] = "fragmented";
        const auto bytes = Remote::encode(registration) + '\n';
        fragmented.write(bytes.left(7)); fragmented.flush(); QTest::qWait(20);
        fragmented.write(bytes.mid(7)); fragmented.flush(); QTRY_VERIFY(fragmented.bytesAvailable() > 0);
        QJsonObject registered; QVERIFY(Remote::decode(fragmented.readLine().trimmed(), registered));
        QVERIFY(!registered["target"].toString().isEmpty());
        Agent a(pipe, "No response"); a.acknowledge = false; QTRY_VERIFY(!a.id.isEmpty());
        Client c(b.port()); QTRY_VERIFY(c.has("ready")); c.claim(a.id); QTRY_VERIFY(c.has("selection", "claimed"));
        c.key(a.id, 1); QTRY_COMPARE(a.keys.size(), 1); QTRY_VERIFY_WITH_TIMEOUT(c.has("result", "timeout"), 4500);
        QTest::qWait(50); QCOMPARE(a.keys.size(), 1);
    }
    void applicationRestoresSelectionWithoutReplayingKeys() {
        Broker b; const auto pipe = pipeName(); QVERIFY(b.start(QHostAddress::LocalHost, 0, pipe));
        Agent a(pipe, "TVTest"); QTRY_VERIFY(!a.id.isEmpty());
        RemoteClient c; QSignalSpy results(&c, &RemoteClient::commandResult);
        c.connectTo(QString("ws://127.0.0.1:%1").arg(b.port()));
        QTRY_VERIFY(c.connected()); QTRY_COMPARE(c.targets().size(), 1);
        c.claim(a.id); QTRY_COMPARE(c.selected(), a.id); c.press("enter"); QTRY_COMPARE(results.size(), 1);
        QCOMPARE(a.keys.size(), 1); c.setActive(false); QVERIFY(c.selected().isEmpty()); QVERIFY(!c.connected());
        c.press("enter");
        c.setActive(true);
        QTRY_VERIFY(c.connected()); QTRY_COMPARE(c.selected(), a.id);
        QTest::qWait(50); QCOMPARE(a.keys.size(), 1);
        c.setActive(true); QVERIFY(c.connected());
        c.press("back"); QTRY_COMPARE(results.size(), 2); QCOMPARE(a.keys.size(), 2);
    }
    void coldStartAndAutoConnectionPreference() {
        Broker b; const auto pipe = pipeName(); QVERIFY(b.start(QHostAddress::LocalHost, 0, pipe));
        Agent a(pipe, "Remembered TVTest"); QTRY_VERIFY(!a.id.isEmpty());
        const auto url = QString("ws://127.0.0.1:%1").arg(b.port());
        {
            RemoteClient first; first.connectTo(url); QTRY_VERIFY(first.connected());
            first.claim(a.id); QTRY_COMPARE(first.selected(), a.id); first.setActive(false);
        }
        RemoteClient restored; QVERIFY(restored.hasSavedConnection()); restored.start();
        QTRY_VERIFY(restored.connected()); QTRY_COMPARE(restored.selected(), a.id);
        restored.start(); QVERIFY(restored.connected()); QCOMPARE(a.keys.size(), 0);
        restored.setAutoConnect(false); restored.setActive(false); restored.setActive(true);
        QTest::qWait(150); QVERIFY(!restored.connected()); QCOMPARE(restored.status(), QStringLiteral("未接続"));
        RemoteClient disabled; QVERIFY(!disabled.autoConnect()); disabled.start();
        QTest::qWait(150); QVERIFY(!disabled.connected());
    }
    void startupRetriesUntilServerBecomesAvailable() {
        QTcpServer reserve; QVERIFY(reserve.listen(QHostAddress::LocalHost, 0));
        const auto port = reserve.serverPort(); reserve.close();
        QSettings().setValue("url", QString("ws://127.0.0.1:%1").arg(port));
        RemoteClient c; c.start(); QTest::qWait(100); QVERIFY(!c.connected());
        Broker b; const auto pipe = pipeName(); QVERIFY(b.start(QHostAddress::LocalHost, port, pipe));
        Agent a(pipe, "Late TVTest");
        QTRY_VERIFY_WITH_TIMEOUT(c.connected(), 5000); QTRY_COMPARE(c.targets().size(), 1);
        QVERIFY(c.selected().isEmpty()); QCOMPARE(a.keys.size(), 0);
    }
    void manualDisconnectAndReleaseAreRespected() {
        Broker b; const auto pipe = pipeName(); QVERIFY(b.start(QHostAddress::LocalHost, 0, pipe));
        Agent a(pipe, "TVTest"); QTRY_VERIFY(!a.id.isEmpty());
        const auto url = QString("ws://127.0.0.1:%1").arg(b.port());
        RemoteClient c; c.connectTo(url); QTRY_VERIFY(c.connected()); c.claim(a.id); QTRY_COMPARE(c.selected(), a.id);
        c.disconnectFromServer(); c.setActive(false); c.setActive(true);
        QTest::qWait(150); QVERIFY(!c.connected());
        c.connectTo(url); QTRY_VERIFY(c.connected()); QTRY_COMPARE(c.selected(), a.id);
        c.release(); QVERIFY(c.selected().isEmpty()); c.setActive(false); c.setActive(true);
        QTRY_VERIFY(c.connected()); QTest::qWait(150); QVERIFY(c.selected().isEmpty()); QCOMPARE(a.keys.size(), 0);
    }
    void releaseWhileSwitchingDoesNotRememberTheNewTarget() {
        Broker b; const auto pipe = pipeName(); QVERIFY(b.start(QHostAddress::LocalHost, 0, pipe));
        Agent a(pipe, "A"), other(pipe, "B"); QTRY_VERIFY(!a.id.isEmpty() && !other.id.isEmpty());
        RemoteClient c; c.connectTo(QString("ws://127.0.0.1:%1").arg(b.port())); QTRY_VERIFY(c.connected());
        c.claim(a.id); QTRY_COMPARE(c.selected(), a.id);
        c.claim(other.id); c.release();
        QTRY_VERIFY(c.status().contains(QStringLiteral("解放しました"))); QVERIFY(!QSettings().contains("targetId"));
        c.setActive(false); c.setActive(true); QTRY_VERIFY(c.connected()); QTest::qWait(100);
        QVERIFY(c.selected().isEmpty()); QCOMPARE(a.keys.size(), 0); QCOMPARE(other.keys.size(), 0);
    }
    void previousTargetOwnedByAnotherRemoteIsNotTaken() {
        Broker b; const auto pipe = pipeName(); QVERIFY(b.start(QHostAddress::LocalHost, 0, pipe));
        Agent a(pipe, "TVTest"); QTRY_VERIFY(!a.id.isEmpty());
        RemoteClient c; c.connectTo(QString("ws://127.0.0.1:%1").arg(b.port())); QTRY_VERIFY(c.connected());
        c.claim(a.id); QTRY_COMPARE(c.selected(), a.id); c.setActive(false);
        Client owner(b.port()); QTRY_VERIFY(owner.has("ready")); owner.claim(a.id); QTRY_VERIFY(owner.has("selection", "claimed"));
        c.setActive(true); QTRY_VERIFY(c.connected());
        QTRY_VERIFY(c.status().contains(QStringLiteral("別のリモコン"))); QVERIFY(c.selected().isEmpty());
        c.press("enter"); QCOMPARE(a.keys.size(), 0);
        owner.send(Remote::message("release")); QTRY_VERIFY(owner.has("selection", "released"));
        QTest::qWait(100); QVERIFY(c.selected().isEmpty());
    }
    void resumeDoesNotChooseAReplacementInstance() {
        Broker b; const auto pipe = pipeName(); QVERIFY(b.start(QHostAddress::LocalHost, 0, pipe));
        Agent original(pipe, "Same name"); QTRY_VERIFY(!original.id.isEmpty());
        RemoteClient c; c.connectTo(QString("ws://127.0.0.1:%1").arg(b.port())); QTRY_VERIFY(c.connected());
        c.claim(original.id); QTRY_COMPARE(c.selected(), original.id); c.setActive(false); original.socket.abort();
        Agent replacement(pipe, "Same name"); QTRY_VERIFY(!replacement.id.isEmpty()); QVERIFY(replacement.id != original.id);
        c.setActive(true); QTRY_VERIFY(c.connected()); QTRY_COMPARE(c.targets().size(), 1);
        QTest::qWait(100); QVERIFY(c.selected().isEmpty()); c.press("enter"); QCOMPARE(replacement.keys.size(), 0);
    }
    void acknowledgementsCannotCrossTargets() {
        Broker b; const auto pipe = pipeName(); QVERIFY(b.start(QHostAddress::LocalHost, 0, pipe));
        Agent a(pipe, "Target"), wrong(pipe, "Wrong target"); a.acknowledge = false;
        QTRY_VERIFY(!a.id.isEmpty() && !wrong.id.isEmpty());
        Client c(b.port()); QTRY_VERIFY(c.has("ready")); c.claim(a.id); QTRY_VERIFY(c.has("selection", "claimed"));
        c.key(a.id, 1); QTRY_COMPARE(a.keys.size(), 1);
        auto forged = Remote::message("result"); forged["request"] = a.keys[0]["request"]; forged["status"] = "dispatched";
        wrong.send(forged); QTest::qWait(50); QVERIFY(!c.has("result"));
        a.send(forged); QTRY_VERIFY(c.has("result", "dispatched"));
    }
    void delayedKeysAreRejected() {
        Broker b; const auto pipe = pipeName(); QVERIFY(b.start(QHostAddress::LocalHost, 0, pipe));
        Agent a(pipe, "Target"); QTRY_VERIFY(!a.id.isEmpty());
        Client c(b.port()); QTRY_VERIFY(c.has("ready")); c.claim(a.id); QTRY_VERIFY(c.has("selection", "claimed"));
        auto o = Remote::message("key"); o["id"] = "stale"; o["sequence"] = 1;
        o["target"] = a.id; o["key"] = "enter"; o["action"] = "press";
        o["expires"] = static_cast<double>(QDateTime::currentMSecsSinceEpoch() - 1); c.send(o);
        QTRY_VERIFY(c.has("result", "expired")); QCOMPARE(a.keys.size(), 0);
    }
    void lanListenerAndSingleBroker() {
        Broker lan; QVERIFY(lan.start(QHostAddress::AnyIPv4, 0, pipeName()));
        const auto pipe = pipeName(); Broker first, second;
        QVERIFY(first.start(QHostAddress::LocalHost, 0, pipe));
        QVERIFY(!second.start(QHostAddress::LocalHost, 0, pipe));
    }
    void portChangePreservesAgentsAndPersistsStartup() {
        Broker b; const auto pipe = pipeName(); QVERIFY(b.start(QHostAddress::LocalHost, 0, pipe));
        Agent agent(pipe, "TVTest", "persistent-target"); QTRY_VERIFY(!agent.id.isEmpty());
        const auto id = agent.id;
        Client old(b.port()); QTRY_VERIFY(old.has("ready")); old.claim(id); QTRY_VERIFY(old.has("selection", "claimed"));
        old.key(id, 1); QTRY_COMPARE(agent.keys.size(), 1);
        QSettings config(m_temporary.path() + "/port.ini", QSettings::IniFormat);
        QSettings registration(m_temporary.path() + "/port-startup.ini", QSettings::IniFormat);
        ServerSettings settings(config, pipe); StartupSettings startup(registration, pipe);
        QString error; QVERIFY(startup.setEnabled(true, "previous-command", error));
        QTcpServer freePort; QVERIFY(freePort.listen(QHostAddress::LocalHost, 0));
        const auto port = freePort.serverPort(); freePort.close();
        QVERIFY(settings.applyPort(b, port, "127.0.0.1", &startup, "C:/Remote App/server.exe", error));
        QCOMPARE(b.port(), port); QTRY_COMPARE(old.socket.state(), QAbstractSocket::UnconnectedState);
        QCOMPARE(agent.id, id); QCOMPARE(agent.socket.state(), QLocalSocket::ConnectedState);
        QCOMPARE(agent.keys.size(), 1);
        Client fresh(port); QTRY_VERIFY(fresh.has("ready")); fresh.claim(id); QTRY_VERIFY(fresh.has("selection", "claimed"));
        fresh.key(id, 1, "red"); QTRY_COMPARE(agent.keys.size(), 2);
        QSettings saved(m_temporary.path() + "/port.ini", QSettings::IniFormat);
        QCOMPARE(ServerSettings(saved, pipe).port(), port); QCOMPARE(ServerSettings(saved, pipe + "-other").port(), quint16(18765));
        QCOMPARE(startup.command(), StartupSettings::makeCommand("C:/Remote App/server.exe", "127.0.0.1", port, pipe));
    }
    void occupiedOrInvalidPortLeavesLiveConnectionAndPreferencesUnchanged() {
        Broker b; const auto pipe = pipeName(); QVERIFY(b.start(QHostAddress::LocalHost, 0, pipe));
        Client client(b.port()); QTRY_VERIFY(client.has("ready")); const auto previous = b.port();
        QSettings config(m_temporary.path() + "/occupied-port.ini", QSettings::IniFormat);
        ServerSettings settings(config, pipe); QString error;
        QVERIFY(settings.applyPort(b, previous, "127.0.0.1", nullptr, {}, error));
        QTcpServer occupied; QVERIFY(occupied.listen(QHostAddress::LocalHost, 0));
        QVERIFY(!settings.applyPort(b, occupied.serverPort(), "127.0.0.1", nullptr, {}, error)); QVERIFY(!error.isEmpty());
        QCOMPARE(b.port(), previous); QCOMPARE(settings.port(), previous);
        QCOMPARE(client.socket.state(), QAbstractSocket::ConnectedState);
        QVERIFY(!settings.applyPort(b, 0, "127.0.0.1", nullptr, {}, error));
        QVERIFY(!settings.applyPort(b, 65536, "127.0.0.1", nullptr, {}, error));
        QCOMPARE(b.port(), previous);
    }
    void portSaveFailureKeepsOldListenerAndRollsBackPreferences() {
        Broker b; const auto pipe = pipeName(); QVERIFY(b.start(QHostAddress::LocalHost, 0, pipe));
        Client client(b.port()); QTRY_VERIFY(client.has("ready")); const auto previous = b.port();
        const auto blockerPath = m_temporary.path() + "/port-blocker";
        QFile blocker(blockerPath); QVERIFY(blocker.open(QIODevice::WriteOnly)); blocker.close();
        QTcpServer freePort; QVERIFY(freePort.listen(QHostAddress::LocalHost, 0)); const auto next = freePort.serverPort(); freePort.close();
        QSettings broken(blockerPath + "/settings.ini", QSettings::IniFormat);
        ServerSettings failed(broken, pipe); QString error;
        QVERIFY(!failed.applyPort(b, next, "127.0.0.1", nullptr, {}, error)); QVERIFY(!error.isEmpty());
        QCOMPARE(b.port(), previous); QCOMPARE(client.socket.state(), QAbstractSocket::ConnectedState);
        QSettings config(m_temporary.path() + "/rollback-port.ini", QSettings::IniFormat);
        ServerSettings settings(config, pipe);
        QVERIFY(settings.applyPort(b, previous, "127.0.0.1", nullptr, {}, error));
        broken.setValue(StartupSettings::entryName(pipe), "previous-startup-command");
        StartupSettings startup(broken, pipe);
        QVERIFY(!settings.applyPort(b, next, "127.0.0.1", &startup, "C:/Remote/server.exe", error));
        QCOMPARE(settings.port(), previous); QCOMPARE(b.port(), previous);
        QCOMPARE(startup.command(), QString("previous-startup-command"));
        QCOMPARE(client.socket.state(), QAbstractSocket::ConnectedState);
        QVERIFY(freePort.listen(QHostAddress::LocalHost, next));
    }
    void startupRegistrationPersistsAndPreservesOtherEntries() {
        const auto file = m_temporary.path() + "/startup.ini";
        QSettings settings(file, QSettings::IniFormat);
        settings.setValue("OtherApplication", "unchanged");
        StartupSettings main(settings, Remote::PipeName), other(settings, "another-profile");
        QVERIFY(!main.enabled()); QVERIFY(!other.enabled());
        QString error;
        const auto command = StartupSettings::makeCommand("C:/Remote App/server.exe", "192.0.2.10", 18765, Remote::PipeName);
        QVERIFY(main.setEnabled(true, command, error)); QVERIFY(error.isEmpty());
        QSettings persisted(file, QSettings::IniFormat);
        StartupSettings reload(persisted, Remote::PipeName); QVERIFY(reload.enabled()); QCOMPARE(reload.command(), command);
        QVERIFY(other.setEnabled(true, "other-command", error));
        QVERIFY(main.setEnabled(true, "new-location-command", error)); QCOMPARE(main.command(), QString("new-location-command"));
        QVERIFY(!main.setEnabled(true, QString(261, 'x'), error)); QVERIFY(!error.isEmpty());
        QCOMPARE(main.command(), QString("new-location-command"));
        QVERIFY(main.setEnabled(false, {}, error)); QVERIFY(!main.enabled());
        QVERIFY(other.enabled()); QCOMPARE(settings.value("OtherApplication").toString(), QString("unchanged"));
    }
    void startupRegistrationReportsWriteFailure() {
        const auto file = m_temporary.path() + "/startup-blocker";
        QFile blocker(file); QVERIFY(blocker.open(QIODevice::WriteOnly)); blocker.close();
        QSettings settings(file + "/settings.ini", QSettings::IniFormat);
        StartupSettings startup(settings, Remote::PipeName);
        QString error;
        QVERIFY(!startup.setEnabled(true, "server-command", error)); QVERIFY(!error.isEmpty());
        QVERIFY(!startup.enabled());
    }
    void serverShutdownCommandAndRestart() {
        const auto pipe = pipeName();
        for (int attempt = 0; attempt < 2; ++attempt) {
            QProcess server;
            server.start(QStringLiteral(REMOTE_SERVER_PATH), {"--no-tray", "--port", "0", "--pipe", pipe});
            QVERIFY(server.waitForStarted(5000));
            QByteArray output;
            QTRY_VERIFY_WITH_TIMEOUT((output += server.readAllStandardOutput()).contains("WS "), 5000);
            QProcess stop;
            if (attempt == 0) {
                stop.start(QStringLiteral(REMOTE_SERVER_PATH), {"--stop", "--pipe", pipe + "-other"});
                QVERIFY(stop.waitForFinished(5000)); QCOMPARE(stop.exitCode(), 1);
                QVERIFY(stop.readAllStandardError().contains("Cannot connect"));
                QCOMPARE(server.state(), QProcess::Running);
                QLocalSocket malformed;
                malformed.connectToServer(pipe + ".control"); QVERIFY(malformed.waitForConnected(1000));
                malformed.write("invalid\n"); malformed.flush();
                QTRY_COMPARE(malformed.state(), QLocalSocket::UnconnectedState);
                QCOMPARE(server.state(), QProcess::Running);
            }
            stop.start(QStringLiteral(REMOTE_SERVER_PATH), {"--stop", "--pipe", pipe});
            QVERIFY(stop.waitForFinished(5000)); QCOMPARE(stop.exitCode(), 0);
            QVERIFY(stop.readAllStandardOutput().contains("shutdown requested"));
            QVERIFY(server.waitForFinished(5000)); QCOMPARE(server.exitStatus(), QProcess::NormalExit);
            QCOMPARE(server.exitCode(), 0);
        }
        Broker replacement; QVERIFY(replacement.start(QHostAddress::LocalHost, 0, pipe));
    }
    void consoleHelpAndErrors() {
        QProcess process;
        process.start(QStringLiteral(REMOTE_SERVER_PATH), {"--help"});
        QVERIFY(process.waitForFinished(5000)); QCOMPARE(process.exitCode(), 0);
        const auto help = process.readAllStandardOutput();
        QVERIFY(help.contains("--port")); QVERIFY(!help.contains("--token-file")); QVERIFY(!help.contains("--cert"));
        process.start(QStringLiteral(REMOTE_SERVER_PATH), {"--unknown-option"});
        QVERIFY(process.waitForFinished(5000)); QCOMPARE(process.exitCode(), 1);
        QVERIFY(!process.readAllStandardError().isEmpty());
    }
#ifdef Q_OS_WIN
    void windowsStartupCommandRoundTripsAndRegistryPersists() {
        const QString executable = "C:/Remote App/server.exe";
        const QString pipe = "living room \\\"quoted\\\"\\";
        const auto command = StartupSettings::makeCommand(executable, "192.0.2.10", 18765, pipe);
        int count = 0;
        auto** args = CommandLineToArgvW(command.toStdWString().c_str(), &count);
        QVERIFY(args != nullptr);
        const auto cleanupArgs = qScopeGuard([&] { LocalFree(args); });
        QCOMPARE(count, 7);
        QCOMPARE(QString::fromWCharArray(args[0]), QString("C:\\Remote App\\server.exe"));
        QCOMPARE(QString::fromWCharArray(args[1]), QString("--address"));
        QCOMPARE(QString::fromWCharArray(args[2]), QString("192.0.2.10"));
        QCOMPARE(QString::fromWCharArray(args[3]), QString("--port"));
        QCOMPARE(QString::fromWCharArray(args[4]), QString("18765"));
        QCOMPARE(QString::fromWCharArray(args[5]), QString("--pipe"));
        QCOMPARE(QString::fromWCharArray(args[6]), pipe);
        // Isolated native registry key: never enable actual Windows startup in tests.
        const auto key = "HKEY_CURRENT_USER\\Software\\nanamitm\\TVTestRemoteTests\\" + pipeName();
        QSettings registry(key, QSettings::NativeFormat);
        const auto cleanupRegistry = qScopeGuard([&] { registry.clear(); registry.sync(); });
        StartupSettings startup(registry, Remote::PipeName); QString error;
        QVERIFY(startup.setEnabled(true, command, error));
        QSettings reloaded(key, QSettings::NativeFormat);
        StartupSettings restored(reloaded, Remote::PipeName); QCOMPARE(restored.command(), command);
        QVERIFY(startup.setEnabled(false, {}, error)); QVERIFY(!registry.contains(StartupSettings::entryName(Remote::PipeName)));
    }
    void persistentPluginTokenIsConfigurable() {
        const auto path = (m_temporary.path() + "/identity.ini").toStdWString();
        std::wstring first, second;
        QVERIFY(TvtRemote::LoadOrCreateTargetToken(path, first)); QVERIFY(TvtRemote::ValidTargetToken(first));
        QVERIFY(TvtRemote::LoadOrCreateTargetToken(path, second)); QCOMPARE(first, second);
        QVERIFY(WritePrivateProfileStringW(L"TVTDataBroadcastingWV2", L"RemoteTargetToken", L"living-room", path.c_str()));
        QVERIFY(TvtRemote::LoadOrCreateTargetToken(path, second)); QCOMPARE(second, std::wstring(L"living-room"));
        QVERIFY(WritePrivateProfileStringW(L"TVTDataBroadcastingWV2", L"RemoteTargetToken", L"invalid token", path.c_str()));
        QVERIFY(!TvtRemote::LoadOrCreateTargetToken(path, second)); QVERIFY(second.empty());
        QVERIFY(!TvtRemote::ValidTargetToken(L"")); QVERIFY(!TvtRemote::ValidTargetToken(std::wstring(65, L'a')));
        QVERIFY(!TvtRemote::LoadOrCreateTargetToken((m_temporary.path() + "/missing/file.ini").toStdWString(), second));
    }
    void nativeBridgeAndReconnect() {
        const auto pipe = pipeName();
        auto b = std::make_unique<Broker>(); QVERIFY(b->start(QHostAddress::LocalHost, 0, pipe));
        TvtRemote::Agent agent;
        agent.UpdateState("Native TVTest", "Native channel", true, "bml", "native-stable");
        QVERIFY(agent.Start(pipe.toStdWString()));
        RemoteClient c; QSignalSpy results(&c, &RemoteClient::commandResult);
        auto connectClient = [&] { c.connectTo(QString("ws://127.0.0.1:%1").arg(b->port())); };
        connectClient(); QTRY_VERIFY(c.connected()); QTRY_COMPARE(c.targets().size(), 1);
        const auto id = c.targets()[0].toMap()["id"].toString();
        c.claim(id); QTRY_COMPARE(c.selected(), id); c.press("red");
        TvtRemote::Agent::Command command;
        bool received = false;
        QTRY_VERIFY((received = received || agent.Next(command))); QCOMPARE(QString::fromStdString(command.key), QString("red"));
        agent.Complete(command, true); QTRY_COMPARE(results.size(), 1);
        QCOMPARE(results[0][1].toString(), QString("dispatched"));
        const auto port = b->port(); b.reset(); QTRY_VERIFY(!c.connected());
        b = std::make_unique<Broker>(); QVERIFY(b->start(QHostAddress::LocalHost, port, pipe));
        QTRY_VERIFY(c.connected()); QTRY_COMPARE_WITH_TIMEOUT(c.targets().size(), 1, 5000);
        const auto newId = c.targets()[0].toMap()["id"].toString(); QVERIFY(newId != id); QTRY_COMPARE(c.selected(), newId);
        agent.Stop(); QTRY_COMPARE(c.targets().size(), 0);
    }
    void windowBridgeDispatchesOnUiThread() {
        Broker b; const auto pipe = pipeName(); QVERIFY(b.start(QHostAddress::LocalHost, 0, pipe));
        TvtRemote::Win32Bridge bridge;
        const auto mainThread = GetCurrentThreadId();
        DWORD dispatchThread = 0; QString pressed;
        QVERIFY(bridge.Start(GetModuleHandleW(nullptr), []() -> nlohmann::json {
            return {{"targetToken", "window-stable"}, {"name", "Window bridge"}, {"channel", "Test"}, {"enabled", true}, {"mode", "bml"}};
        }, [&](const std::string& key) {
            dispatchThread = GetCurrentThreadId(); pressed = QString::fromStdString(key); return true;
        }, pipe.toStdWString()));
        RemoteClient c; QSignalSpy results(&c, &RemoteClient::commandResult);
        c.connectTo(QString("ws://127.0.0.1:%1").arg(b.port()));
        QTRY_VERIFY(c.connected()); QTRY_COMPARE(c.targets().size(), 1);
        QCOMPARE(c.targets()[0].toMap()["targetToken"].toString(), QString("window-stable"));
        c.claim(c.targets()[0].toMap()["id"].toString()); QTRY_VERIFY(!c.selected().isEmpty());
        c.press("data"); QTRY_COMPARE(pressed, QString("data")); QCOMPARE(dispatchThread, mainThread);
        QTRY_COMPARE(results.size(), 1);
        QVERIFY(c.controls().isEmpty()); // no controls advertised by this bridge
        bridge.Stop(); QTRY_COMPARE(c.targets().size(), 0);
    }
    void windowBridgeAppliesControlsAndReportsState() {
        Broker b; const auto pipe = pipeName(); QVERIFY(b.start(QHostAddress::LocalHost, 0, pipe));
        TvtRemote::Win32Bridge bridge;
        const auto mainThread = GetCurrentThreadId();
        DWORD controlThread = 0; bool caption = false; int volume = 30; bool mute = false;
        QVERIFY(bridge.Start(GetModuleHandleW(nullptr), [&]() -> nlohmann::json {
            return {{"targetToken", "control-bridge"}, {"name", "Control bridge"}, {"channel", "Test"}, {"enabled", true},
                {"mode", "bml"}, {"controls", {{"caption", caption}, {"volume", volume}, {"mute", mute}}}};
        }, [](const std::string&) { return false; }, [&](const std::string& name, const nlohmann::json& value) {
            controlThread = GetCurrentThreadId();
            if (name == "caption") caption = value.get<bool>();
            else if (name == "volume") volume = value.get<int>();
            else if (name == "mute") mute = value.get<bool>();
            else return false;
            return true;
        }, pipe.toStdWString()));
        RemoteClient c; QSignalSpy results(&c, &RemoteClient::commandResult);
        c.connectTo(QString("ws://127.0.0.1:%1").arg(b.port()));
        QTRY_VERIFY(c.connected()); QTRY_COMPARE(c.targets().size(), 1);
        c.claim(c.targets()[0].toMap()["id"].toString()); QTRY_VERIFY(!c.selected().isEmpty());
        QCOMPARE(c.controls().value("volume").toInt(), 30);
        c.control("volume", 72); QTRY_COMPARE(volume, 72); QCOMPARE(controlThread, mainThread);
        // The bridge refreshes state right after a control, well before the 1 s poll.
        QTRY_COMPARE_WITH_TIMEOUT(c.controls().value("volume").toInt(), 72, 800);
        c.control("caption", true); QTRY_VERIFY(caption); QTRY_VERIFY(c.controls().value("caption").toBool());
        c.control("mute", true); QTRY_VERIFY(mute);
        QTRY_COMPARE(results.size(), 3);
        for (const auto& r : results) QCOMPARE(r[1].toString(), QString("dispatched"));
        bridge.Stop(); QTRY_COMPARE(c.targets().size(), 0);
    }
    void windowBridgePagesChannelListAndTunes() {
        // A large tuning space is split into pages below the message limit.
        auto makeSpaces = [] {
            nlohmann::json bs = nlohmann::json::array();
            for (int i = 0; i < 150; ++i)
                bs.push_back({{"space", 1}, {"index", i}, {"remoteControlKey", 0}, {"name", "BS channel " + std::to_string(i)},
                    {"serviceId", 100 + i}, {"networkId", 4}, {"transportStreamId", 16400 + i},
                    {"program", {{"name", std::string(60, 'x')}, {"start", 1790000000000LL}, {"duration", 3600}}}});
            return nlohmann::json::array({
                {{"space", 0}, {"name", "UHF"}, {"channels", {{{"space", 0}, {"index", 0}, {"remoteControlKey", 1}, {"name", "General"},
                    {"serviceId", 1024}, {"networkId", 32736}, {"transportStreamId", 32736}}}}},
                {{"space", 1}, {"name", "BS"}, {"channels", bs}},
                {{"space", 2}, {"name", "Empty"}, {"channels", nlohmann::json::array()}}});
        };
        const auto pages = TvtRemote::Win32Bridge::PaginateChannels(makeSpaces());
        QVERIFY(pages.size() > 2); std::size_t bsCount = 0;
        for (const auto& page : pages) {
            QVERIFY(page.dump().size() < 12000);
            QVERIFY(page["space"] != 2); // empty spaces are omitted
            if (page["space"] == 1) { QCOMPARE(page["order"].get<int>(), 1); bsCount += page["channels"].size(); }
        }
        QCOMPARE(bsCount, std::size_t(150));

        Broker b; const auto pipe = pipeName(); QVERIFY(b.start(QHostAddress::LocalHost, 0, pipe));
        TvtRemote::Win32Bridge bridge;
        nlohmann::json tuned;
        QVERIFY(bridge.Start(GetModuleHandleW(nullptr), [&]() -> nlohmann::json {
            return {{"targetToken", "channel-bridge"}, {"name", "Channel bridge"}, {"channel", "Test"}, {"enabled", true}, {"mode", "bml"},
                {"controls", {{"channel", {{"space", 0}, {"index", -1}, {"serviceId", 1024}, {"networkId", 32736}, {"transportStreamId", 32736}}}}}};
        }, [](const std::string&) { return false; }, [&](const std::string& name, const nlohmann::json& value) {
            if (name != "channel") return false;
            tuned = value; return true;
        }, makeSpaces, pipe.toStdWString()));
        RemoteClient c;
        c.connectTo(QString("ws://127.0.0.1:%1").arg(b.port()));
        QTRY_VERIFY(c.connected()); QTRY_COMPARE(c.targets().size(), 1);
        c.claim(c.targets()[0].toMap()["id"].toString()); QTRY_VERIFY(!c.selected().isEmpty());
        c.requestChannels(); QTRY_VERIFY(!c.channelsLoading());
        const auto spaces = c.channelSpaces(); QCOMPARE(spaces.size(), 2);
        const auto bsChannels = spaces[1].toMap().value("channels").toList(); QCOMPARE(bsChannels.size(), 150);
        c.selectChannel(bsChannels[42].toMap()); QTRY_VERIFY(tuned.is_object());
        QCOMPARE(tuned["serviceId"].get<int>(), 142); QCOMPARE(tuned["index"].get<int>(), 42); QCOMPARE(tuned.size(), std::size_t(5));
        bridge.Stop(); QTRY_COMPARE(c.targets().size(), 0);
    }
#endif
};
QTEST_GUILESS_MAIN(Integration)
#include "integration.moc"
