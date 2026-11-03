#pragma once
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QString>

namespace Remote {
inline constexpr int Version = 1;
inline constexpr int MaxMessage = 16384;
inline const QString PipeName = QStringLiteral("TVTDataBroadcastingRemote-v1");
inline QJsonObject message(const char* type) {
    return {{"v", Version}, {"type", QLatin1String(type)}};
}
inline QByteArray encode(const QJsonObject& object) {
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}
inline bool decode(const QByteArray& bytes, QJsonObject& object) {
    if (bytes.size() > MaxMessage) return false;
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) return false;
    object = doc.object();
    return object.value("v").toInt(-1) == Version && object.value("type").isString();
}
inline bool validKey(const QString& key) {
    static const QSet<QString> keys = {"data", "up", "down", "left", "right", "enter",
        "back", "blue", "red", "green", "yellow", "0", "1", "2", "3", "4", "5",
        "6", "7", "8", "9", "10", "11", "12"};
    return keys.contains(key);
}
inline QJsonObject result(const QString& id, const QString& status) {
    auto o = message("result"); o["id"] = id; o["status"] = status; return o;
}
}
