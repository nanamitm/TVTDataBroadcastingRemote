#include "RemoteClient.h"
#include <QDateTime>
#include <QHostAddress>
#include <QJsonArray>
#include <QSettings>
#include <QSslCertificate>
#include <QUuid>

RemoteClient::RemoteClient(QObject* parent) : QObject(parent) {
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
        m_alive.start(); m_watchdog.start();
        // Verify pinned certificates even when the platform CA verification succeeded.
        if (m_url.scheme() == "wss" && !m_pin.isEmpty()) {
            const auto cert = m_socket.sslConfiguration().peerCertificate();
            if (cert.digest(QCryptographicHash::Sha256) != m_pin
                || cert.expiryDate() < QDateTime::currentDateTimeUtc()
                || cert.effectiveDate() > QDateTime::currentDateTimeUtc()) {
                m_failure = QStringLiteral("証明書の指紋または有効期間が一致しません");
                m_wanted = false; m_socket.abort(); setStatus(m_failure); return;
            }
        }
        m_sequence = 0;
        auto o = Remote::message("auth"); o["token"] = m_token; send(o);
        setStatus(QStringLiteral("認証中"));
    });
    connect(&m_socket, &QWebSocket::sslErrors, this, [this](const QList<QSslError>& errors) {
        const auto cert = m_socket.sslConfiguration().peerCertificate();
        const auto timestamp = QDateTime::currentDateTimeUtc();
        if (!m_pin.isEmpty() && !cert.isNull() && cert.digest(QCryptographicHash::Sha256) == m_pin
            && cert.effectiveDate() <= timestamp && timestamp <= cert.expiryDate()) {
            // The explicitly entered certificate pin is the trust anchor, including IP connections.
            m_socket.ignoreSslErrors(errors);
        } else {
            m_failure = QStringLiteral("TLS証明書を確認してください");
            m_wanted = false; m_socket.abort(); setStatus(m_failure);
        }
    });
    connect(&m_socket, &QWebSocket::textMessageReceived, this, &RemoteClient::received);
    connect(&m_socket, &QWebSocket::disconnected, this, [this] {
        m_watchdog.stop();
        m_authenticated = false; m_selected.clear(); m_targets.clear();
        if (m_socket.closeCode() == QWebSocketProtocol::CloseCodePolicyViolated) m_wanted = false;
        if (m_wanted) { setStatus(QStringLiteral("切断・再接続待ち（操作先の再選択が必要）")); m_reconnect.start(); }
        else if (!m_failure.isEmpty()) setStatus(m_failure);
        else if (m_socket.closeReason() == "Authentication locked; restart server")
            setStatus(QStringLiteral("認証に5回失敗しました。PCの中継サーバーを再起動してください"));
        else if (m_socket.closeReason() == "Authentication failed")
            setStatus(QStringLiteral("接続コードが一致しません"));
        else setStatus(QStringLiteral("切断しました: ") + m_socket.closeReason());
    });
    connect(&m_socket, &QWebSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        setStatus(m_failure.isEmpty() ? QStringLiteral("接続エラー: ") + m_socket.errorString() : m_failure);
        if (m_wanted) m_reconnect.start();
    });
}
QString RemoteClient::savedUrl() const { return QSettings().value("url", "wss://192.168.1.10:18765").toString(); }
QString RemoteClient::savedFingerprint() const { return QSettings().value("fingerprint").toString(); }
void RemoteClient::setStatus(const QString& text) { m_status = text; emit changed(); }
void RemoteClient::connectTo(const QString& url, const QString& token, const QString& fingerprint) {
    disconnectFromServer();
    const QUrl parsed(url.trimmed());
    const bool loopback = parsed.host() == "localhost" || QHostAddress(parsed.host()).isLoopback();
    if (!parsed.isValid() || parsed.host().isEmpty() || !parsed.userInfo().isEmpty()
        || !parsed.query().isEmpty() || !parsed.fragment().isEmpty()
        || (parsed.path() != "" && parsed.path() != "/")
        || (parsed.scheme() != "wss" && !(parsed.scheme() == "ws" && loopback))) {
        setStatus(QStringLiteral("LAN接続には wss://IP:ポート を指定してください")); return;
    }
    QString normalized = fingerprint.trimmed(); normalized.remove(':'); normalized.remove(' ');
    if (!normalized.isEmpty()) {
        if (normalized.size() != 64 || QByteArray::fromHex(normalized.toLatin1()).toHex() != normalized.toLower().toLatin1()) {
            setStatus(QStringLiteral("証明書指紋はSHA-256の64桁で指定してください")); return;
        }
    }
    if (!Remote::validToken(token.trimmed().toUtf8())) {
        setStatus(QStringLiteral("3桁の接続コードを入力してください")); return;
    }
    m_url = parsed; m_token = token.trimmed(); m_pin = QByteArray::fromHex(normalized.toLatin1());
    QSettings settings; settings.setValue("url", parsed.toString()); settings.setValue("fingerprint", normalized);
    m_wanted = true; setStatus(QStringLiteral("接続中")); m_socket.open(m_url);
}
void RemoteClient::disconnectFromServer() {
    m_wanted = false; m_failure.clear(); m_reconnect.stop(); m_watchdog.stop(); m_socket.abort();
    m_authenticated = false; m_selected.clear(); m_targets.clear(); m_token.clear(); setStatus(QStringLiteral("未接続"));
}
void RemoteClient::setActive(bool active) { if (!active) disconnectFromServer(); }
void RemoteClient::send(const QJsonObject& o) {
    if (m_socket.state() == QAbstractSocket::ConnectedState) m_socket.sendTextMessage(QString::fromUtf8(Remote::encode(o)));
}
void RemoteClient::claim(const QString& target) {
    if (!m_authenticated || target.isEmpty()) return;
    auto o = Remote::message("claim"); o["target"] = target; send(o);
}
void RemoteClient::release() {
    m_selected.clear(); emit changed();
    if (m_authenticated) send(Remote::message("release"));
}
void RemoteClient::press(const QString& key) {
    if (!m_authenticated || m_selected.isEmpty() || !Remote::validKey(key)) return;
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
    if (type == "authenticated") {
        m_serverTime = static_cast<qint64>(o["serverTime"].toDouble()); m_serverClock.start();
        m_authenticated = true; setStatus(QStringLiteral("接続済み・操作先を選択してください"));
    }
    else if (type == "targets") {
        m_targets = o["targets"].toArray().toVariantList();
        bool found = false;
        for (const auto& t : m_targets) if (t.toMap()["id"].toString() == m_selected) found = true;
        if (!found) m_selected.clear();
        emit changed();
    } else if (type == "selection") {
        const auto status = o["status"].toString();
        m_selected = o["target"].toString();
        if (status == "claimed") setStatus(QStringLiteral("操作先を選択しました"));
        else if (status == "busy") setStatus(QStringLiteral("別のリモコンが操作中です"));
        else { m_selected.clear(); setStatus(status == "target_gone" ? QStringLiteral("操作先が終了しました") : QStringLiteral("操作権を解放しました")); }
    } else if (type == "result") {
        const auto status = o["status"].toString(); emit commandResult(o["id"].toString(), status);
        if (status != "dispatched") setStatus(QStringLiteral("操作結果: ") + status);
    }
}
