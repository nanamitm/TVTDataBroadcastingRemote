#include "RemoteClient.h"
#include <QJsonArray>
#include <QSettings>
#include <QUuid>
#include <cmath>

RemoteClient::RemoteClient(QObject* parent) : QObject(parent) {
    // Migrate the previous connection URL and discard the obsolete setting.
    QSettings settings;
    QUrl previous(settings.value("url").toString());
    if (previous.scheme() == "wss") { previous.setScheme("ws"); settings.setValue("url", previous.toString()); }
    settings.remove("fingerprint");
    m_reconnect.setSingleShot(true); m_reconnect.setInterval(2000);
    m_watchdog.setInterval(2000);
    connect(&m_watchdog, &QTimer::timeout, this, [this] {
        if (m_alive.elapsed() > 6000) { m_socket.abort(); return; }
        m_socket.ping();
    });
    connect(&m_socket, &QWebSocket::pong, this, [this] { m_alive.restart(); });
    m_socket.setMaxAllowedIncomingMessageSize(Remote::MaxMessage);
    m_socket.setMaxAllowedIncomingFrameSize(Remote::MaxMessage);
    connect(&m_reconnect, &QTimer::timeout, this, [this] { if (m_wanted) m_socket.open(m_url); });
    connect(&m_socket, &QWebSocket::connected, this, [this] {
        m_alive.start(); m_watchdog.start(); m_sequence = 0;
        setStatus(QStringLiteral("接続中"));
    });
    connect(&m_socket, &QWebSocket::textMessageReceived, this, &RemoteClient::received);
    connect(&m_socket, &QWebSocket::disconnected, this, [this] {
        m_watchdog.stop(); m_ready = false; m_selected.clear(); m_targets.clear();
        if (m_socket.closeCode() == QWebSocketProtocol::CloseCodePolicyViolated) m_wanted = false;
        if (m_wanted) { setStatus(QStringLiteral("切断・再接続待ち（操作先の再選択が必要）")); m_reconnect.start(); }
        else setStatus(QStringLiteral("切断しました: ") + m_socket.closeReason());
    });
    connect(&m_socket, &QWebSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        setStatus(QStringLiteral("接続エラー: ") + m_socket.errorString());
        if (m_wanted) m_reconnect.start();
    });
}
QString RemoteClient::savedUrl() const { return QSettings().value("url", "ws://192.168.1.10:18765").toString(); }
void RemoteClient::setStatus(const QString& text) { m_status = text; emit changed(); }
void RemoteClient::connectTo(const QString& url) {
    disconnectFromServer();
    const QUrl parsed(url.trimmed());
    if (!parsed.isValid() || parsed.host().isEmpty() || !parsed.userInfo().isEmpty()
        || !parsed.query().isEmpty() || !parsed.fragment().isEmpty()
        || (parsed.path() != "" && parsed.path() != "/") || parsed.scheme() != "ws") {
        setStatus(QStringLiteral("ws://IP:ポート を指定してください")); return;
    }
    m_url = parsed; QSettings().setValue("url", parsed.toString());
    m_wanted = true; setStatus(QStringLiteral("接続中")); m_socket.open(m_url);
}
void RemoteClient::disconnectFromServer() {
    m_wanted = false; m_reconnect.stop(); m_watchdog.stop(); m_socket.abort();
    m_ready = false; m_selected.clear(); m_targets.clear(); setStatus(QStringLiteral("未接続"));
}
void RemoteClient::setActive(bool active) { if (!active) disconnectFromServer(); }
void RemoteClient::send(const QJsonObject& o) {
    if (m_socket.state() == QAbstractSocket::ConnectedState) m_socket.sendTextMessage(QString::fromUtf8(Remote::encode(o)));
}
void RemoteClient::claim(const QString& target) {
    if (!m_ready || target.isEmpty()) return;
    auto o = Remote::message("claim"); o["target"] = target; send(o);
}
void RemoteClient::release() {
    m_selected.clear(); emit changed();
    if (m_ready) send(Remote::message("release"));
}
void RemoteClient::press(const QString& key) {
    if (!m_ready || m_selected.isEmpty() || !Remote::validKey(key)) return;
    if (m_socket.bytesToWrite() > 8192) { setStatus(QStringLiteral("送信待ちのため操作を破棄しました")); return; }
    auto o = Remote::message("key"); o["target"] = m_selected; o["key"] = key;
    o["action"] = "press"; o["sequence"] = static_cast<double>(++m_sequence);
    o["expires"] = static_cast<double>(m_serverTime + m_serverClock.elapsed() + 1500);
    o["id"] = QUuid::createUuid().toString(QUuid::WithoutBraces); send(o);
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
        m_serverTime = static_cast<qint64>(serverTime); m_serverClock.start();
        m_ready = true; setStatus(QStringLiteral("接続済み・操作先を選択してください"));
    } else if (type == "targets") {
        m_targets = o["targets"].toArray().toVariantList();
        bool found = false;
        for (const auto& t : m_targets) if (t.toMap()["id"].toString() == m_selected) found = true;
        if (!found) m_selected.clear();
        emit changed();
    } else if (type == "selection") {
        const auto status = o["status"].toString(); m_selected = o["target"].toString();
        if (status == "claimed") setStatus(QStringLiteral("操作先を選択しました"));
        else if (status == "busy") setStatus(QStringLiteral("別のリモコンが操作中です"));
        else { m_selected.clear(); setStatus(status == "target_gone" ? QStringLiteral("操作先が終了しました") : QStringLiteral("操作権を解放しました")); }
    } else if (type == "result") {
        const auto status = o["status"].toString(); emit commandResult(o["id"].toString(), status);
        if (status != "dispatched") setStatus(QStringLiteral("操作結果: ") + status);
    }
}
