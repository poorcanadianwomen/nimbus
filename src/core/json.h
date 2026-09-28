#pragma once

#include <QtCore/QJsonObject>
#include <QtCore/QJsonValue>
#include <QtCore/QJsonArray>
#include <QtCore/QString>

namespace nimbus {

inline QString jsonString(const QJsonObject& o, const QString& key, const QString& def = {}) {
    const QJsonValue v = o.value(key);
    return v.isString() ? v.toString() : def;
}

inline qint64 jsonInt(const QJsonObject& o, const QString& key, qint64 def = 0) {
    const QJsonValue v = o.value(key);
    return v.isDouble() ? qint64(v.toDouble()) : def;
}

inline bool jsonBool(const QJsonObject& o, const QString& key, bool def = false) {
    const QJsonValue v = o.value(key);
    return v.isBool() ? v.toBool() : def;
}

inline QJsonObject jsonObject(const QJsonObject& o, const QString& key) {
    const QJsonValue v = o.value(key);
    return v.isObject() ? v.toObject() : QJsonObject{};
}

inline QJsonArray jsonArray(const QJsonObject& o, const QString& key) {
    const QJsonValue v = o.value(key);
    return v.isArray() ? v.toArray() : QJsonArray{};
}

inline double jsonDouble(const QJsonObject& o, const QString& key, double def = 0.0) {
    const QJsonValue v = o.value(key);
    return v.isDouble() ? v.toDouble() : def;
}

} // namespace nimbus