#include "RemoteClient.h"
#include <QJsonArray>
#include <QSettings>
#include <QUuid>
#include <algorithm>
#include <cmath>

namespace {
bool validWsUrl(const QUrl& url) {
    return url.isValid() && !url.host().isEmpty() && url.userInfo().isEmpty()
        && url.query().isEmpty() && url.fragment().isEmpty()
        && (url.path().isEmpty() || url.path() == "/") && url.scheme() == "ws";
}
}

RemoteClient::RemoteClient(QObject* parent) : QObject(parent) {
    // Migrate the previous connection URL and discard the obsolete setting.
    QSettings settings;
    QUrl previous(settings.value("url").toString());
    if (previous.scheme() == "wss") { previous.setScheme("ws"); settings.setValue("url", previous.toString()); }
    settings.remove("fingerprint");
    m_autoConnect = settings.value("autoConnect", true).toBool();
    m_reconnect.setSingleShot(true); m_reconnect.setInterval(2000);
    m_connectTimeout.setSingleShot(true); m_connectTimeout.setInterval(5000);
    connect(&m_connectTimeout, &QTimer::timeout, this, [this] {
        if (!m_wanted || !m_active || m_ready) return;
        m_socket.abort();
        setStatus(QStringLiteral("接続がタイムアウトしました。再接続待ち"));
        m_reconnect.start();
    });
    // Volume drags send at most ~7 updates per second; the last value always wins.
    m_volumeThrottle.setSingleShot(true); m_volumeThrottle.setInterval(150);
    connect(&m_volumeThrottle, &QTimer::timeout, this, [this] {
        if (m_pendingVolume < 0) return;
        const auto volume = m_pendingVolume; m_pendingVolume = -1;
        sendVolume(volume); m_volumeThrottle.start();
    });
    m_watchdog.setInterval(2000);
    connect(&m_watchdog, &QTimer::timeout, this, [this] {
        if (m_alive.elapsed() > 6000) { m_socket.abort(); return; }
        m_socket.ping();
    });
    connect(&m_socket, &QWebSocket::pong, this, [this] { m_alive.restart(); });
    m_socket.setMaxAllowedIncomingMessageSize(Remote::MaxMessage);
    m_socket.setMaxAllowedIncomingFrameSize(Remote::MaxMessage);
    connect(&m_reconnect, &QTimer::timeout, this, &RemoteClient::openConnection);
    connect(&m_socket, &QWebSocket::connected, this, [this] {
        m_alive.start(); m_watchdog.start(); m_sequence = 0;
        setStatus(QStringLiteral("接続中"));
    });
    connect(&m_socket, &QWebSocket::textMessageReceived, this, &RemoteClient::received);
    connect(&m_socket, &QWebSocket::disconnected, this, [this] {
        m_connectTimeout.stop(); m_watchdog.stop(); m_ready = false; m_selected.clear(); m_targets.clear();
        if (m_socket.closeCode() == QWebSocketProtocol::CloseCodePolicyViolated) {
            m_wanted = false; m_userDisconnected = true;
        }
        if (m_wanted && m_active) { setStatus(QStringLiteral("切断・再接続待ち")); m_reconnect.start(); }
        else setStatus(QStringLiteral("切断しました: ") + m_socket.closeReason());
    });
    connect(&m_socket, &QWebSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        m_connectTimeout.stop();
        setStatus(QStringLiteral("接続エラー: ") + m_socket.errorString());
        if (m_wanted && m_active) m_reconnect.start();
    });
}
RemoteClient::~RemoteClient() {
    m_socket.disconnect(this);
    m_reconnect.stop(); m_watchdog.stop(); m_connectTimeout.stop(); m_socket.abort();
}
QString RemoteClient::savedUrl() const { return QSettings().value("url", "ws://192.168.1.10:18765").toString(); }
bool RemoteClient::hasSavedConnection() const {
    const auto value = QSettings().value("url").toString();
    return !value.isEmpty() && validWsUrl(QUrl(value));
}
void RemoteClient::start() {
    if (m_started) return;
    m_started = true;
    if (m_autoConnect && m_active && hasSavedConnection()) connectTo(savedUrl());
}
void RemoteClient::setAutoConnect(bool enabled) {
    if (m_autoConnect == enabled) return;
    m_autoConnect = enabled; QSettings().setValue("autoConnect", enabled);
    if (enabled) m_userDisconnected = false;
    emit changed();
    if (enabled && m_started && m_active && !m_wanted && hasSavedConnection()) connectTo(savedUrl());
}
void RemoteClient::setStatus(const QString& text) { m_status = text; emit changed(); }
void RemoteClient::connectTo(const QString& url) {
    m_started = true; m_userDisconnected = true; stopConnection();
    QUrl parsed(url.trimmed());
    if (!validWsUrl(parsed)) {
        setStatus(QStringLiteral("ws://IP:ポート を指定してください")); return;
    }
    parsed.setPath({}); m_url = parsed;
    QSettings settings;
    m_lastTarget = settings.value("targetUrl").toString() == parsed.toString()
        ? settings.value("targetId").toString() : QString();
    m_lastToken = settings.value("targetUrl").toString() == parsed.toString()
        ? settings.value("targetToken").toString() : QString();
    settings.setValue("url", parsed.toString());
    m_userDisconnected = false; m_wanted = m_active;
    if (m_active) openConnection();
    else setStatus(QStringLiteral("バックグラウンド中"));
}
void RemoteClient::stopConnection() {
    m_wanted = false; m_reconnect.stop(); m_watchdog.stop(); m_connectTimeout.stop(); m_socket.abort();
    m_ready = false; m_selected.clear(); m_targets.clear(); m_restorePending.clear();
    m_volumeThrottle.stop(); m_pendingVolume = -1; m_requestedVolume = -1;
    clearChannels();
}
void RemoteClient::clearChannels() {
    if (m_channelSpaces.isEmpty() && m_channelRequest.isEmpty() && m_channelPages.isEmpty() && m_channelDrivers.isEmpty()) return;
    m_channelSpaces.clear(); m_channelRequest.clear(); m_channelPages.clear();
    m_channelDrivers.clear(); m_channelDriver.clear(); m_pendingDrivers.clear(); m_pendingDriver.clear();
    emit channelsChanged();
}
void RemoteClient::disconnectFromServer() {
    m_userDisconnected = true; stopConnection(); setStatus(QStringLiteral("未接続"));
}
void RemoteClient::openConnection() {
    if (!m_wanted || !m_active || m_socket.state() != QAbstractSocket::UnconnectedState) return;
    m_restorePending = m_lastTarget; m_connectTimeout.start();
    setStatus(QStringLiteral("接続中")); m_socket.open(m_url);
}
void RemoteClient::setActive(bool active) {
    if (m_active == active) return;
    m_active = active;
    if (!active) {
        stopConnection();
        setStatus(m_userDisconnected ? QStringLiteral("未接続") : QStringLiteral("バックグラウンド中"));
    } else if (m_started && m_autoConnect && !m_userDisconnected && hasSavedConnection()) {
        connectTo(savedUrl());
    } else if (!m_wanted && !m_ready) {
        setStatus(QStringLiteral("未接続"));
    }
}
void RemoteClient::send(const QJsonObject& o) {
    if (m_socket.state() == QAbstractSocket::ConnectedState) m_socket.sendTextMessage(QString::fromUtf8(Remote::encode(o)));
}
void RemoteClient::claim(const QString& target) {
    if (!m_ready || target.isEmpty()) return;
    m_restorePending.clear();
    auto o = Remote::message("claim"); o["target"] = target; send(o);
}
void RemoteClient::release() {
    m_lastTarget.clear(); m_lastToken.clear(); m_restorePending.clear();
    QSettings settings; settings.remove("targetId"); settings.remove("targetToken"); settings.remove("targetUrl");
    m_selected.clear(); clearChannels(); emit changed();
    if (m_ready) send(Remote::message("release"));
}
QString RemoteClient::sendCommand(QJsonObject o) {
    if (!m_active || !m_ready || m_selected.isEmpty()) return {};
    if (m_socket.bytesToWrite() > 8192) { setStatus(QStringLiteral("送信待ちのため操作を破棄しました")); return {}; }
    const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    o["target"] = m_selected; o["sequence"] = static_cast<double>(++m_sequence);
    o["expires"] = static_cast<double>(m_serverTime + m_serverClock.elapsed() + 1500);
    o["id"] = id; send(o);
    return id;
}
void RemoteClient::requestChannels(const QString& driver) {
    if (!controls().contains("channel") || !m_channelRequest.isEmpty()) return;
    if (!driver.isEmpty() && !Remote::validDriver(driver)) return;
    m_channelPages.clear(); m_pendingDrivers.clear(); m_pendingDriver.clear();
    auto o = Remote::message("channels");
    if (!driver.isEmpty()) o["driver"] = driver;
    m_channelRequest = sendCommand(o);
    emit channelsChanged();
}
void RemoteClient::selectChannel(const QVariantMap& channel) {
    // Only the identifying fields are sent; the plugin re-checks them before tuning.
    QVariantMap value;
    for (const auto* key : {"space", "index", "serviceId", "networkId", "transportStreamId"})
        value[QLatin1String(key)] = channel.value(QLatin1String(key));
    // The listed BonDriver; the plugin switches to it first when it is not in use.
    if (!m_channelDriver.isEmpty()) value["driver"] = m_channelDriver;
    control("channel", value);
}
void RemoteClient::press(const QString& key) {
    if (!Remote::validKey(key)) return;
    auto o = Remote::message("key"); o["key"] = key; o["action"] = "press"; sendCommand(o);
}
QVariantMap RemoteClient::controls() const {
    if (m_selected.isEmpty()) return {};
    for (const auto& t : m_targets) {
        const auto target = t.toMap();
        if (target.value("id").toString() == m_selected) return target.value("controls").toMap();
    }
    return {};
}
void RemoteClient::sendVolume(int volume) {
    auto o = Remote::message("control"); o["name"] = "volume"; o["value"] = volume; sendCommand(o);
}
void RemoteClient::control(const QString& name, const QVariant& value) {
    if (!controls().contains(name)) return;
    if (name == "volume") {
        bool ok = false; const auto volume = value.toInt(&ok);
        if (!ok || volume < 0 || volume > Remote::MaxVolume) return;
        m_requestedVolume = volume; m_volumeRequestedAt.start();
        if (m_volumeThrottle.isActive()) { m_pendingVolume = volume; return; }
        sendVolume(volume); m_volumeThrottle.start(); return;
    }
    const auto json = QJsonValue::fromVariant(value);
    if (!Remote::validControl(name, json)) return;
    auto o = Remote::message("control"); o["name"] = name; o["value"] = json; sendCommand(o);
}
void RemoteClient::stepVolume(int delta) {
    // Repeated steps build on the last requested value until the plugin's state
    // catches up, so holding the button does not resend the same volume.
    auto base = m_requestedVolume >= 0 && m_volumeRequestedAt.isValid() && m_volumeRequestedAt.elapsed() < 1500
        ? m_requestedVolume : controls().value("volume", -1).toInt();
    if (base < 0) return;
    control("volume", std::clamp(base + delta, 0, Remote::MaxVolume));
}
bool RemoteClient::volumeKey(int key, bool autoRepeat) {
    if (key != Qt::Key_VolumeUp && key != Qt::Key_VolumeDown && key != Qt::Key_VolumeMute) return false;
    const auto available = controls();
    if (key == Qt::Key_VolumeMute) {
        if (!autoRepeat && available.contains("mute")) {
            control("mute", !available.value("mute").toBool()); emit volumeKeyPressed();
        }
        return true;
    }
    if (!available.contains("volume")) return true;
    // Android repeats a held key about every 50 ms; advance at the on-screen
    // button's pace instead of racing to 0 or 100.
    if (!autoRepeat || !m_volumeKeyRepeat.isValid() || m_volumeKeyRepeat.elapsed() >= 150) {
        m_volumeKeyRepeat.start();
        stepVolume(key == Qt::Key_VolumeUp ? 5 : -5);
    }
    emit volumeKeyPressed();
    return true;
}
void RemoteClient::restoreSelection() {
    if (!m_ready || !m_selected.isEmpty() || m_restorePending.isEmpty()) return;
    QVariantMap match;
    int matches = 0;
    for (const auto& value : m_targets) {
        const auto target = value.toMap();
        if (m_lastToken.isEmpty() ? target.value("id").toString() != m_restorePending
            : target.value("targetToken").toString() != m_lastToken) continue;
        match = target; ++matches;
    }
    if (matches == 0) return;
    if (matches > 1 || match.value("identityConflict").toBool()) {
        setStatus(QStringLiteral("操作先識別トークンが重複しています。設定を確認してください")); return;
    }
    m_restorePending.clear();
    if (match.value("busy").toBool()) {
        setStatus(QStringLiteral("前回のTVTestは別のリモコンが操作中です")); return;
    }
    auto o = Remote::message("claim"); o["target"] = match.value("id").toString();
    if (!m_lastToken.isEmpty()) o["targetToken"] = m_lastToken;
    send(o);
}
void RemoteClient::received(const QString& text) {
    QJsonObject o;
    if (!Remote::decode(text.toUtf8(), o)) { disconnectFromServer(); setStatus(QStringLiteral("通信仕様が一致しません")); return; }
    const auto type = o["type"].toString();
    if (type == "ready") {
        const auto serverTime = o["serverTime"].toDouble();
        if (!o["serverTime"].isDouble() || !std::isfinite(serverTime)
            || serverTime <= 0 || serverTime > 9007199254740991.0) {
            disconnectFromServer(); setStatus(QStringLiteral("サーバー時刻が不正です")); return;
        }
        m_connectTimeout.stop(); m_serverTime = static_cast<qint64>(serverTime); m_serverClock.start();
        m_ready = true; setStatus(QStringLiteral("接続済み・操作先を選択してください"));
    } else if (type == "targets") {
        m_targets = o["targets"].toArray().toVariantList();
        bool found = false;
        for (const auto& t : m_targets) if (t.toMap()["id"].toString() == m_selected) {
            found = true;
            const auto token = t.toMap()["targetToken"].toString();
            if (token != m_lastToken) { m_lastToken = token; QSettings().setValue("targetToken", token); }
        }
        if (!found) { m_selected.clear(); clearChannels(); }
        emit changed();
        restoreSelection();
    } else if (type == "selection") {
        const auto status = o["status"].toString();
        if (m_selected != o["target"].toString()) clearChannels();
        m_selected = o["target"].toString();
        if (status == "claimed") {
            m_lastTarget = m_selected; m_lastToken.clear();
            for (const auto& t : m_targets) if (t.toMap()["id"].toString() == m_selected)
                m_lastToken = t.toMap()["targetToken"].toString();
            QSettings settings; settings.setValue("targetId", m_selected); settings.setValue("targetToken", m_lastToken); settings.setValue("targetUrl", m_url.toString());
            setStatus(QStringLiteral("操作先を選択しました"));
        }
        else if (status == "busy") setStatus(QStringLiteral("別のリモコンが操作中です"));
        else {
            m_selected.clear();
            if (status == "target_gone" && !m_lastToken.isEmpty()) m_restorePending = m_lastTarget;
            if (status == "identity_conflict") m_restorePending = m_lastTarget;
            if (status == "released") {
                m_lastTarget.clear(); m_lastToken.clear(); m_restorePending.clear();
                QSettings settings; settings.remove("targetId"); settings.remove("targetToken"); settings.remove("targetUrl");
            }
            setStatus(status == "identity_conflict" ? QStringLiteral("操作先識別トークンが重複しています。設定を確認してください")
                : status == "target_gone" ? QStringLiteral("TVTestの再起動を待っています") : QStringLiteral("操作権を解放しました"));
        }
    } else if (type == "channelDrivers") {
        if (m_channelRequest.isEmpty() || o["id"].toString() != m_channelRequest) return;
        m_pendingDrivers.clear();
        for (const auto& name : o["drivers"].toArray()) m_pendingDrivers.append(name.toString());
        m_pendingDriver = o["driver"].toString();
    } else if (type == "channelPage") {
        if (m_channelRequest.isEmpty() || o["id"].toString() != m_channelRequest) return;
        auto& space = m_channelPages[o["order"].toInt()];
        space["space"] = o["space"].toInt(); space["name"] = o["spaceName"].toString();
        auto channels = space.value("channels").toList();
        channels.append(o["channels"].toArray().toVariantList());
        space["channels"] = channels;
    } else if (type == "result") {
        const auto id = o["id"].toString();
        const auto status = o["status"].toString(); emit commandResult(id, status);
        if (!m_channelRequest.isEmpty() && id == m_channelRequest) {
            m_channelRequest.clear();
            if (status == "dispatched") {
                m_channelSpaces.clear();
                for (const auto& space : std::as_const(m_channelPages)) m_channelSpaces.append(space);
                // Older plugins send no BonDriver list; the list then is the current one.
                m_channelDrivers = m_pendingDrivers; m_channelDriver = m_pendingDriver;
            }
            m_pendingDrivers.clear(); m_pendingDriver.clear();
            m_channelPages.clear();
            emit channelsChanged();
        }
        if (status != "dispatched") setStatus(QStringLiteral("操作結果: ") + status);
    }
}
