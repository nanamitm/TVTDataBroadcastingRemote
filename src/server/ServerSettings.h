#pragma once
#include <QSettings>
class Broker;
class StartupSettings;

class ServerSettings {
public:
    ServerSettings(QSettings& settings, const QString& pipe);
    quint16 port() const;
    bool applyPort(Broker& broker, int port, const QString& address, StartupSettings* startup,
                   const QString& executable, QString& error);
private:
    QSettings& m_settings;
    QString m_key;
    QString m_pipe;
};
