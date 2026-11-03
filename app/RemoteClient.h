#pragma once
#include "Protocol.h"
#include <QTimer>
#include <QElapsedTimer>
#include <QVariantList>
#include <QWebSocket>

class RemoteClient : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool connected READ connected NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString selected READ selected NOTIFY changed)
    Q_PROPERTY(QVariantList targets READ targets NOTIFY changed)
    Q_PROPERTY(QString savedUrl READ savedUrl CONSTANT)
public:
    explicit RemoteClient(QObject* parent = nullptr);
    bool connected() const { return m_ready; }
    QString status() const { return m_status; }
    QString selected() const { return m_selected; }
    QVariantList targets() const { return m_targets; }
    QString savedUrl() const;
    Q_INVOKABLE void connectTo(const QString& url);
    Q_INVOKABLE void disconnectFromServer();
    Q_INVOKABLE void claim(const QString& target);
    Q_INVOKABLE void release();
    Q_INVOKABLE void press(const QString& key);
    Q_INVOKABLE void setActive(bool active);
signals:
    void changed();
    void commandResult(QString id, QString status);
private:
    void send(const QJsonObject& o);
    void received(const QString& text);
    void setStatus(const QString& text);
    QWebSocket m_socket;
    QTimer m_reconnect;
    QTimer m_watchdog;
    QElapsedTimer m_alive;
    QElapsedTimer m_serverClock;
    qint64 m_serverTime = 0;
    QUrl m_url;
    QString m_selected;
    QString m_status = QStringLiteral("未接続");
    QVariantList m_targets;
    quint64 m_sequence = 0;
    bool m_ready = false;
    bool m_wanted = false;
};
