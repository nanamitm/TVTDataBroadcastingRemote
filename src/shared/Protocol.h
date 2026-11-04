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
inline bool validTargetToken(const QString& token) {
    if (token.size() > 64) return false;
    for (const auto c : token) {
        const auto v = c.unicode();
        if (!((v >= 'a' && v <= 'z') || (v >= 'A' && v <= 'Z')
            || (v >= '0' && v <= '9') || v == '-' || v == '_' || v == '.')) return false;
    }
    return true; // Empty tokens keep compatibility with older plugins.
}
inline bool validKey(const QString& key) {
    static const QSet<QString> keys = {"data", "up", "down", "left", "right", "enter",
        "back", "blue", "red", "green", "yellow", "0", "1", "2", "3", "4", "5",
        "6", "7", "8", "9", "10", "11", "12"};
    return keys.contains(key);
}
// Value-carrying controls (protocol 0.5.0). Agents advertise the controls they
// support in state.controls; the broker forwards only advertised controls so
// older plugins never receive an IPC message type they do not understand.
inline constexpr int MaxVolume = 100;
inline bool integerIn(const QJsonValue& value, double low, double high) {
    if (!value.isDouble()) return false;
    const auto v = value.toDouble();
    return v >= low && v <= high && v == static_cast<double>(static_cast<qint64>(v));
}
// A channel (0.6.0) is identified by its TVTest tuning space and channel-list
// index, plus the service and stream IDs that must still match when tuning.
// In state.controls the current channel may report -1 for unknown positions.
inline bool validChannel(const QJsonValue& value, bool allowUnknown) {
    if (!value.isObject()) return false;
    const auto o = value.toObject();
    const double first = allowUnknown ? -1 : 0;
    return o.size() == 5 && integerIn(o["space"], first, 255) && integerIn(o["index"], first, 9999)
        && integerIn(o["serviceId"], 0, 65535) && integerIn(o["networkId"], 0, 65535)
        && integerIn(o["transportStreamId"], 0, 65535);
}
// `state` is true for values reported by an agent, false for requests.
inline bool validControl(const QString& name, const QJsonValue& value, bool state = false) {
    if (name == "caption" || name == "mute") return value.isBool();
    if (name == "volume") return integerIn(value, 0, MaxVolume);
    if (name == "channel") return validChannel(value, state);
    return false;
}
// Keeps only known controls with valid values; unknown names are dropped so
// newer plugins remain compatible with this broker.
inline QJsonObject controlState(const QJsonObject& controls) {
    QJsonObject result;
    for (auto it = controls.begin(); it != controls.end(); ++it)
        if (validControl(it.key(), it.value(), true)) result[it.key()] = it.value();
    return result;
}
// Channel-list pages (0.6.0) are relayed from the agent to the requesting
// client; each page belongs to one tuning space.
inline constexpr int MaxChannelPageEntries = 200;
inline QJsonObject result(const QString& id, const QString& status) {
    auto o = message("result"); o["id"] = id; o["status"] = status; return o;
}
}
