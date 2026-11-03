#include "ServerSettings.h"
#include "Broker.h"
#include "StartupSettings.h"
#include <QCryptographicHash>

ServerSettings::ServerSettings(QSettings& settings, const QString& pipe) : m_settings(settings),
    m_key("listeners/" + QString::fromLatin1(QCryptographicHash::hash(pipe.toUtf8(), QCryptographicHash::Sha256).toHex()) + "/port"), m_pipe(pipe) {}
quint16 ServerSettings::port() const {
    bool ok = false; const auto value = m_settings.value(m_key, 18765).toInt(&ok);
    return ok && value >= 1 && value <= 65535 ? static_cast<quint16>(value) : 18765;
}
bool ServerSettings::applyPort(Broker& broker, int port, const QString& address, StartupSettings* startup,
                               const QString& executable, QString& error) {
    error.clear();
    if (port < 1 || port > 65535) { error = QStringLiteral("ポート番号は1～65535で指定してください。"); return false; }
    const auto previous = m_settings.value(m_key);
    const auto restore = [&] {
        if (previous.isValid()) m_settings.setValue(m_key, previous);
        else m_settings.remove(m_key);
        m_settings.sync();
    };
    const bool changed = broker.changePort(static_cast<quint16>(port), [&](QString& failure) {
        m_settings.setValue(m_key, port); m_settings.sync();
        if (m_settings.status() != QSettings::NoError) {
            restore(); failure = QStringLiteral("待受ポートの設定を保存できませんでした。元のポートで稼働を続けます。"); return false;
        }
        if (startup && startup->enabled() && !startup->setEnabled(true,
            StartupSettings::makeCommand(executable, address, static_cast<quint16>(port), m_pipe), failure)) {
            restore(); return false;
        }
        return true;
    });
    if (!changed) error = broker.errorString();
    return changed;
}
