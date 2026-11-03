#pragma once
#include <QLocalServer>
#include <functional>

// Local process management, independent of the LAN remote-control protocol.
class ServerControl : public QObject {
public:
    explicit ServerControl(QObject* parent = nullptr);
    bool start(const QString& pipe, std::function<void()> stop);
    QString errorString() const { return m_server.errorString(); }
    static bool requestStop(const QString& pipe, QString& error);
private:
    QLocalServer m_server;
    std::function<void()> m_stop;
};
