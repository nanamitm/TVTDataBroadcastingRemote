#include "StartupSettings.h"
#include "Protocol.h"
#include <QCryptographicHash>
#include <QDir>

namespace {
QString quoteWindowsArgument(const QString& argument) {
    QString result = "\"";
    int slashes = 0;
    for (const auto c : argument) {
        if (c == '\\') { ++slashes; continue; }
        result += QString(c == '"' ? slashes * 2 + 1 : slashes, '\\');
        result += c; slashes = 0;
    }
    result += QString(slashes * 2, '\\');
    return result + '"';
}
}
StartupSettings::StartupSettings(QSettings& settings, const QString& pipe)
    : m_settings(settings), m_entry(entryName(pipe)) {}
QString StartupSettings::entryName(const QString& pipe) {
    if (pipe == Remote::PipeName) return "TVTestRemoteServer";
    return "TVTestRemoteServer-" + QString::fromLatin1(QCryptographicHash::hash(pipe.toUtf8(), QCryptographicHash::Sha256).toHex());
}
bool StartupSettings::enabled() const { return !command().isEmpty(); }
QString StartupSettings::command() const { return m_settings.value(m_entry).toString(); }
QString StartupSettings::makeCommand(const QString& executable, const QString& address, quint16 port, const QString& pipe) {
    return quoteWindowsArgument(QDir::toNativeSeparators(executable))
        + " --address " + quoteWindowsArgument(address) + " --port " + QString::number(port)
        + " --pipe " + quoteWindowsArgument(pipe);
}
bool StartupSettings::setEnabled(bool enabled, const QString& command, QString& error) {
    error.clear();
    // Windows documents a 260-character limit for Run value command lines.
    if (enabled && (command.isEmpty() || command.size() > 260 || command.contains(QChar::Null))) {
        error = QStringLiteral("自動起動のコマンドが長すぎるか不正です。実行ファイルを短いパスへ配置してください。"); return false;
    }
    const auto previous = m_settings.value(m_entry);
    if (enabled) m_settings.setValue(m_entry, command);
    else m_settings.remove(m_entry);
    m_settings.sync();
    if (m_settings.status() == QSettings::NoError && (enabled ? this->command() == command : !m_settings.contains(m_entry))) return true;
    if (previous.isValid()) m_settings.setValue(m_entry, previous);
    else m_settings.remove(m_entry);
    m_settings.sync();
    error = QStringLiteral("Windowsの自動起動設定を保存できませんでした（設定ストアへのアクセスに失敗）。");
    return false;
}
