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
    Q_PROPERTY(bool hasSavedConnection READ hasSavedConnection NOTIFY changed)
    Q_PROPERTY(bool autoConnect READ autoConnect WRITE setAutoConnect NOTIFY changed)
public:
    explicit RemoteClient(QObject* parent = nullptr);
    ~RemoteClient() override;
    bool connected() const { return m_ready; }
    QString status() const { return m_status; }
    QString selected() const { return m_selected; }
    QVariantList targets() const { return m_targets; }
    QString savedUrl() const;
    bool hasSavedConnection() const;
    bool autoConnect() const { return m_autoConnect; }
    void setAutoConnect(bool enabled);
    Q_INVOKABLE void start();
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
    void stopConnection();
    void openConnection();
    void restoreSelection();
    QWebSocket m_socket;
    QTimer m_reconnect;
    QTimer m_watchdog;
    QTimer m_connectTimeout;
    QElapsedTimer m_alive;
    QElapsedTimer m_serverClock;
    qint64 m_serverTime = 0;
    QUrl m_url;
    QString m_selected;
    QString m_lastTarget;
    QString m_lastToken;
    QString m_restorePending;
    QString m_status = QStringLiteral("未接続");
    QVariantList m_targets;
    quint64 m_sequence = 0;
    bool m_ready = false;
    bool m_wanted = false;
    bool m_autoConnect = true;
    bool m_active = true;
    bool m_started = false;
    bool m_userDisconnected = false;
};
