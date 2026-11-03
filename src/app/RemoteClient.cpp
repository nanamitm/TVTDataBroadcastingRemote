#include "RemoteClient.h"
#include <QJsonArray>
#include <QSettings>
#include <QUuid>
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
    m_selected.clear(); emit changed();
    if (m_ready) send(Remote::message("release"));
}
void RemoteClient::press(const QString& key) {
    if (!m_active || !m_ready || m_selected.isEmpty() || !Remote::validKey(key)) return;
    if (m_socket.bytesToWrite() > 8192) { setStatus(QStringLiteral("送信待ちのため操作を破棄しました")); return; }
    auto o = Remote::message("key"); o["target"] = m_selected; o["key"] = key;
    o["action"] = "press"; o["sequence"] = static_cast<double>(++m_sequence);
    o["expires"] = static_cast<double>(m_serverTime + m_serverClock.elapsed() + 1500);
    o["id"] = QUuid::createUuid().toString(QUuid::WithoutBraces); send(o);
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
        if (!found) m_selected.clear();
        emit changed();
        restoreSelection();
    } else if (type == "selection") {
        const auto status = o["status"].toString(); m_selected = o["target"].toString();
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
    } else if (type == "result") {
        const auto status = o["status"].toString(); emit commandResult(o["id"].toString(), status);
        if (status != "dispatched") setStatus(QStringLiteral("操作結果: ") + status);
    }
}
