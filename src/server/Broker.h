#pragma once
#include "Protocol.h"
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <memory>
#include <functional>
#include <QPointer>
#include <QTimer>
#include <QWebSocket>
#include <QWebSocketServer>

class Broker : public QObject {
    Q_OBJECT
public:
    explicit Broker(QObject* parent = nullptr);
    ~Broker() override;
    bool start(const QHostAddress& address, quint16 port, const QString& pipe);
    quint16 port() const { return m_ws->serverPort(); }
    bool changePort(quint16 port, const std::function<bool(QString&)>& save = {});
    QString errorString() const { return m_error; }
private:
    struct Agent { QByteArray buffer; QString id; QJsonObject info; qint64 last = 0; };
    struct Client { QString selected; qint64 last = 0;
        qint64 window = 0; int count = 0; quint64 lastSequence = 0; };
    struct Pending { QPointer<QWebSocket> client; QPointer<QLocalSocket> agent;
        QString id; qint64 expires; };
    void acceptAgents();
    void acceptClients();
    void agentRead(QLocalSocket* socket);
    void agentMessage(QLocalSocket* socket, const QJsonObject& o);
    void clientMessage(QWebSocket* socket, const QString& text);
    void broadcastTargets();
    void sendTargets(QWebSocket* socket);
    void release(QWebSocket* socket);
    void tick();
    static void send(QWebSocket* socket, const QJsonObject& o);
    static void send(QLocalSocket* socket, const QJsonObject& o);
    std::unique_ptr<QWebSocketServer> createListener();
    std::unique_ptr<QWebSocketServer> m_ws;
    QLocalServer m_local;
    QTimer m_timer;
    std::unique_ptr<QLockFile> m_lock;
    QHash<QLocalSocket*, Agent> m_agents;
    QHash<QWebSocket*, Client> m_clients;
    QHash<QString, Pending> m_pending;
    QHash<QString, QWebSocket*> m_owners;
    QString m_error;
};
