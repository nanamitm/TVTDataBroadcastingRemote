#include "Broker.h"
#include <QDateTime>
#include <QDir>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QUuid>
#include <cmath>

namespace {
qint64 now() { return QDateTime::currentMSecsSinceEpoch(); }
QString uuid() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
}
Broker::Broker(QObject* parent)
    : QObject(parent),
      m_local(this) {
    connect(&m_local, &QLocalServer::newConnection, this, &Broker::acceptAgents);
    m_ws = createListener();
    connect(&m_timer, &QTimer::timeout, this, &Broker::tick);
    m_local.setSocketOptions(QLocalServer::UserAccessOption);
    m_local.setMaxPendingConnections(16);
}
std::unique_ptr<QWebSocketServer> Broker::createListener() {
    auto listener = std::make_unique<QWebSocketServer>(QStringLiteral("TVTest Remote"), QWebSocketServer::NonSecureMode, this);
    listener->setHandshakeTimeout(5000); listener->setMaxPendingConnections(8);
    connect(listener.get(), &QWebSocketServer::newConnection, this, &Broker::acceptClients);
    return listener;
}
bool Broker::changePort(quint16 port, const std::function<bool(QString&)>& save) {
    m_error.clear();
    if (!port) { m_error = QStringLiteral("ポート番号は1～65535で指定してください。"); return false; }
    if (port == this->port()) return !save || save(m_error);
    auto next = createListener();
    if (!next->listen(m_ws->serverAddress(), port)) { m_error = QStringLiteral("ポート%1で待受できません: %2").arg(port).arg(next->errorString()); return false; }
    if (save && !save(m_error)) return false;
    // Reserve and save the new listener before touching the live one.
    auto previous = std::move(m_ws); m_ws = std::move(next);
    previous->close();
    for (auto* socket : m_clients.keys()) socket->abort();
    return true;
}
bool Broker::start(const QHostAddress& address, quint16 port, const QString& pipe) {
    // Windows permits multiple named-pipe servers with the same name. Serialize
    // brokers explicitly so TVTest instances never attach to different brokers.
    const auto directory = QDir::tempPath() + "/TVTDataBroadcastingRemote";
    if (!QDir().mkpath(directory)) { m_error = "Cannot create broker lock directory"; return false; }
    m_lock = std::make_unique<QLockFile>(directory + '/' + QString::fromLatin1(
        QCryptographicHash::hash(pipe.toUtf8(), QCryptographicHash::Sha256).toHex()) + ".lock");
    m_lock->setStaleLockTime(0);
    if (!m_lock->tryLock()) { m_error = "A broker already owns this IPC name (or its lock is inaccessible)"; return false; }
    if (!m_local.listen(pipe)) { m_error = "IPC: " + m_local.errorString(); m_lock.reset(); return false; }
    if (!m_ws->listen(address, port)) { m_error = "WebSocket: " + m_ws->errorString(); m_local.close(); m_lock.reset(); return false; }
    m_timer.start(1000);
    return true;
}
Broker::~Broker() {
    m_timer.stop();
    for (auto* s : m_clients.keys()) { s->disconnect(this); s->abort(); delete s; }
    for (auto* s : m_agents.keys()) { s->disconnect(this); s->abort(); delete s; }
    m_ws->close(); m_local.close();
}
void Broker::send(QWebSocket* s, const QJsonObject& o) {
    if (!s || s->state() != QAbstractSocket::ConnectedState) return;
    if (s->bytesToWrite() > 65536) {
        QMetaObject::invokeMethod(s, &QWebSocket::abort, Qt::QueuedConnection); return;
    }
    s->sendTextMessage(QString::fromUtf8(Remote::encode(o)));
}
void Broker::send(QLocalSocket* s, const QJsonObject& o) {
    if (!s || s->state() != QLocalSocket::ConnectedState) return;
    if (s->bytesToWrite() > 65536) {
        QMetaObject::invokeMethod(s, &QLocalSocket::abort, Qt::QueuedConnection); return;
    }
    s->write(Remote::encode(o) + '\n');
}
void Broker::acceptAgents() {
    while (auto* s = m_local.nextPendingConnection()) {
        if (m_agents.size() >= 16) { s->abort(); s->deleteLater(); continue; }
        s->setReadBufferSize(Remote::MaxMessage + 1);
        m_agents.insert(s, Agent{{}, {}, {}, now()});
        connect(s, &QLocalSocket::readyRead, this, [this, s] { agentRead(s); });
        connect(s, &QLocalSocket::disconnected, this, [this, s] {
            const auto id = m_agents.take(s).id;
            if (auto* owner = m_owners.take(id)) {
                m_clients[owner].selected.clear();
                auto o = Remote::message("selection"); o["status"] = "target_gone"; send(owner, o);
            }
            const auto pending = m_pending.keys();
            for (const auto& request : pending) if (m_pending[request].agent == s) {
                const auto p = m_pending.take(request); send(p.client, Remote::result(p.id, "target_gone"));
            }
            broadcastTargets(); s->deleteLater();
        });
    }
}
void Broker::agentRead(QLocalSocket* s) {
    auto& buffer = m_agents[s].buffer;
    buffer += s->readAll();
    for (;;) {
        const auto end = buffer.indexOf('\n');
        if (end < 0) break;
        QJsonObject o;
        const auto line = buffer.left(end); buffer.remove(0, end + 1);
        if (!Remote::decode(line, o)) { s->abort(); return; }
        agentMessage(s, o);
        if (s->state() != QLocalSocket::ConnectedState) return;
    }
    if (buffer.size() >= Remote::MaxMessage) s->abort();
}
void Broker::agentMessage(QLocalSocket* s, const QJsonObject& o) {
    auto& a = m_agents[s];
    const auto type = o["type"].toString();
    if (a.id.isEmpty() && type != "register") { s->abort(); return; }
    a.last = now();
    if (type == "register" || type == "state") {
        if (type == "register" && !a.id.isEmpty()) { s->abort(); return; }
        const auto token = o["targetToken"].toString();
        if ((o.contains("targetToken") && !o["targetToken"].isString()) || !Remote::validTargetToken(token)) { s->abort(); return; }
        if (type == "register") a.id = uuid();
        a.info = {{"id", a.id}, {"name", o["name"].toString().left(100)},
            {"targetToken", token}, {"channel", o["channel"].toString().left(100)}, {"pid", o["pid"].toInt()},
            {"enabled", o["enabled"].toBool()}, {"mode", o["mode"].toString().left(20)}};
        if (o.contains("controls")) {
            if (!o["controls"].isObject()) { s->abort(); return; }
            a.info["controls"] = Remote::controlState(o["controls"].toObject());
        }
        if (type == "register") { auto reply = Remote::message("registered"); reply["target"] = a.id; send(s, reply); }
        broadcastTargets();
    } else if (type == "result") {
        const auto request = o["request"].toString();
        if (!m_pending.contains(request) || m_pending[request].agent != s) return;
        const auto p = m_pending.take(request);
        const auto status = o["status"].toString();
        send(p.client, Remote::result(p.id, status == "dispatched" || status == "unavailable" ? status : "rejected"));
    } else if (type != "heartbeat") s->abort();
}
void Broker::acceptClients() {
    while (auto* s = m_ws->nextPendingConnection()) {
        if (m_clients.size() >= 16) { s->close(); s->deleteLater(); continue; }
        s->setMaxAllowedIncomingMessageSize(Remote::MaxMessage);
        s->setMaxAllowedIncomingFrameSize(Remote::MaxMessage);
        m_clients.insert(s, Client{{}, now()});
        connect(s, &QWebSocket::textMessageReceived, this, [this, s](const QString& text) { clientMessage(s, text); });
        connect(s, &QWebSocket::binaryMessageReceived, this, [s] { s->close(); });
        connect(s, &QWebSocket::pong, this, [this, s] { if (m_clients.contains(s)) m_clients[s].last = now(); });
        connect(s, &QWebSocket::disconnected, this, [this, s] {
            release(s); m_clients.remove(s);
            const auto requests = m_pending.keys();
            for (const auto& request : requests) if (m_pending[request].client == s) m_pending.remove(request);
            s->deleteLater();
        });
        auto ready = Remote::message("ready");
        ready["serverTime"] = static_cast<double>(now());
        send(s, ready); sendTargets(s);
    }
}
void Broker::release(QWebSocket* s) {
    auto it = m_clients.find(s);
    if (it == m_clients.end()) return;
    if (m_owners.value(it->selected) == s) m_owners.remove(it->selected);
    it->selected.clear();
    broadcastTargets();
}
void Broker::clientMessage(QWebSocket* s, const QString& text) {
    QJsonObject o;
    if (!Remote::decode(text.toUtf8(), o)) { s->close(QWebSocketProtocol::CloseCodeProtocolError, "Invalid protocol"); return; }
    auto& c = m_clients[s];
    const auto type = o["type"].toString();
    const auto timestamp = now();
    if (timestamp - c.window >= 1000) { c.window = timestamp; c.count = 0; }
    if (++c.count > 30) { s->close(QWebSocketProtocol::CloseCodePolicyViolated, "Rate limit"); return; }
    c.last = timestamp;
    if (type == "list") { sendTargets(s); return; }
    if (type == "release") {
        release(s); auto r = Remote::message("selection"); r["status"] = "released"; send(s, r); return;
    }
    if (type == "claim") {
        const auto target = o["target"].toString();
        bool found = false;
        for (const auto& a : std::as_const(m_agents)) if (a.id == target && !target.isEmpty()) found = true;
        QString status = "target_gone";
        if (found && o.contains("targetToken")) {
            const auto token = o["targetToken"].toString();
            int matches = 0; bool sameTarget = false;
            for (const auto& a : std::as_const(m_agents)) if (!a.id.isEmpty() && a.info["targetToken"] == token) {
                ++matches; sameTarget = sameTarget || a.id == target;
            }
            if (token.isEmpty() || !Remote::validTargetToken(token) || matches != 1 || !sameTarget) {
                auto r = Remote::message("selection"); r["status"] = "identity_conflict"; r["target"] = c.selected; send(s, r); return;
            }
        }
        if (found) {
            if (m_owners.contains(target) && m_owners[target] != s) status = "busy";
            else { release(s); c.selected = target; m_owners[target] = s; status = "claimed"; }
        }
        auto r = Remote::message("selection"); r["status"] = status;
        r["target"] = status == "claimed" ? target : c.selected; send(s, r); broadcastTargets(); return;
    }
    if (type != "key" && type != "control") { s->close(QWebSocketProtocol::CloseCodeProtocolError, "Unknown message"); return; }
    const auto id = o["id"].toString();
    const auto sequence = o["sequence"].toDouble();
    if (id.isEmpty() || id.size() > 64 || !o["sequence"].isDouble() || !std::isfinite(sequence)
        || sequence < 1 || sequence > 9007199254740991.0 || std::floor(sequence) != sequence
        || sequence <= static_cast<double>(c.lastSequence)) {
        send(s, Remote::result(id.left(64), "invalid_sequence")); return;
    }
    c.lastSequence = static_cast<quint64>(sequence);
    const double expires = o["expires"].toDouble();
    if (!o["expires"].isDouble() || !std::isfinite(expires) || std::floor(expires) != expires
        || expires <= static_cast<double>(timestamp) || expires > static_cast<double>(timestamp + 5000)) {
        send(s, Remote::result(id, "expired")); return;
    }
    const bool isControl = type == "control";
    if (isControl ? !Remote::validControl(o["name"].toString(), o["value"])
                  : (!Remote::validKey(o["key"].toString()) || o["action"] != "press")) {
        send(s, Remote::result(id, isControl ? "invalid_control" : "invalid_key")); return;
    }
    const auto target = o["target"].toString();
    if (target.isEmpty() || c.selected != target || m_owners.value(target) != s) {
        send(s, Remote::result(id, "not_owner")); return;
    }
    int count = 0;
    for (const auto& p : std::as_const(m_pending)) if (p.client == s) ++count;
    if (count >= 8 || m_pending.size() >= 128) { send(s, Remote::result(id, "busy")); return; }
    for (auto it = m_agents.begin(); it != m_agents.end(); ++it) if (it->id == target) {
        // Agents that do not advertise a control (including pre-0.5 plugins,
        // which would disconnect on an unknown IPC type) never receive it.
        if (isControl && !it->info["controls"].toObject().contains(o["name"].toString())) {
            send(s, Remote::result(id, "unsupported")); return;
        }
        const auto request = uuid();
        auto command = Remote::message(isControl ? "control" : "key"); command["request"] = request;
        if (isControl) { command["name"] = o["name"]; command["value"] = o["value"]; }
        else { command["key"] = o["key"]; command["action"] = "press"; }
        const auto deadline = std::min(timestamp + 2000, static_cast<qint64>(expires));
        command["expires"] = static_cast<double>(deadline);
        m_pending.insert(request, Pending{s, it.key(), id, deadline});
        send(it.key(), command); return;
    }
    send(s, Remote::result(id, "target_gone"));
}
void Broker::sendTargets(QWebSocket* s) {
    QJsonArray targets;
    for (const auto& a : std::as_const(m_agents)) if (!a.id.isEmpty()) {
        auto info = a.info; info["busy"] = m_owners.contains(a.id);
        int matches = 0;
        const auto token = info["targetToken"].toString();
        if (!token.isEmpty()) for (const auto& other : std::as_const(m_agents))
            if (!other.id.isEmpty() && other.info["targetToken"] == token) ++matches;
        info["identityConflict"] = matches > 1;
        targets.append(info);
    }
    auto o = Remote::message("targets"); o["targets"] = targets; send(s, o);
}
void Broker::broadcastTargets() {
    for (auto* s : m_clients.keys()) sendTargets(s);
}
void Broker::tick() {
    const auto timestamp = now();
    const auto agents = m_agents.keys();
    for (auto* s : agents) if (timestamp - m_agents[s].last > (m_agents[s].id.isEmpty() ? 5000 : 15000)) s->abort();
    const auto clients = m_clients.keys();
    for (auto* s : clients) {
        const auto c = m_clients.value(s);
        if (timestamp - c.last > 15000) s->abort();
        else s->ping();
    }
    const auto requests = m_pending.keys();
    for (const auto& request : requests) if (m_pending[request].expires <= timestamp) {
        const auto p = m_pending.take(request); send(p.client, Remote::result(p.id, "timeout"));
    }
}
