#pragma once
#include <QSettings>

// The settings store is injected so tests never modify actual Windows startup.
class StartupSettings {
public:
    StartupSettings(QSettings& settings, const QString& pipe);
    bool enabled() const;
    QString command() const;
    bool setEnabled(bool enabled, const QString& command, QString& error);
    static QString entryName(const QString& pipe);
    static QString makeCommand(const QString& executable, const QString& address, quint16 port, const QString& pipe);
private:
    QSettings& m_settings;
    QString m_entry;
};
