// SPDX-FileCopyrightText: Copyright 2026 Resident Evil Revelations Custom Server Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "config.h"

#include "json_writer.h"

#include <algorithm>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QSet>

namespace {

constexpr qint64 U8Max = 0xff;
constexpr qint64 I32Max = 0x7fffffff;

struct FieldSpec {
    const char* name;
    qint64 max;
};

struct RewardType {
    int id;
    const char* name;
    QVector<FieldSpec> fields;
};

constexpr qint64 MaxReliefType = 9;
constexpr qint64 MaxPartsId = 45;
constexpr qint64 MaxWeaponId = 18;

const QVector<RewardType>& rewardTypes() {
    static const QVector<RewardType> types = {
        {1, "relief", {{"relief_type", MaxReliefType}, {"relief_count", U8Max}}},
        {2, "gacha", {{"gacha_type", U8Max}, {"gacha_grade", U8Max}}},
        {3, "parts", {{"parts_id", MaxPartsId}}},
        {4,
         "weapon",
         {{"weapon_id", MaxWeaponId}, {"weapon_rarity", U8Max}, {"weapon_slot", U8Max}}},
    };
    return types;
}

enum class EventKind { Scalar, IntArray, StringArray };

struct EventKey {
    const char* name;
    EventKind kind;
    qint64 max;
    int maxCount;
};

const QVector<EventKey>& eventKeys() {
    static const QVector<EventKey> keys = {
        {"subjugation_enemy_id", EventKind::IntArray, U8Max, 20},
        {"subjugation_exp", EventKind::IntArray, I32Max, 20},
        {"weekend_type", EventKind::Scalar, U8Max, 0},
        {"weekend_discount_rate", EventKind::Scalar, I32Max, 0},
        {"weekend_item_item_id", EventKind::IntArray, U8Max, 5},
        {"weekend_item_price", EventKind::IntArray, I32Max, 5},
        {"participate_serial_id", EventKind::Scalar, I32Max, 0},
        {"participate_event_id", EventKind::Scalar, U8Max, 0},
        {"special_serial_id", EventKind::Scalar, I32Max, 0},
        {"special_event_id", EventKind::Scalar, U8Max, 0},
        {"special_exp_rate", EventKind::Scalar, I32Max, 0},
        {"special_wanted_rate", EventKind::Scalar, I32Max, 0},
        {"discount_event_id", EventKind::Scalar, U8Max, 0},
        {"discount_exp_rate", EventKind::Scalar, I32Max, 0},
        {"collaboration_wanted_name", EventKind::StringArray, 15, 10},
        {"collaboration_wanted_serial_id", EventKind::IntArray, I32Max, 10},
    };
    return keys;
}

bool readInt(const QJsonValue& v, qint64 min, qint64 max, qint64* out) {
    if (!v.isDouble()) {
        return false;
    }
    const double d = v.toDouble();
    if (d != static_cast<double>(static_cast<qint64>(d))) {
        return false;
    }
    const qint64 i = static_cast<qint64>(d);
    if (i < min || i > max) {
        return false;
    }
    *out = i;
    return true;
}

bool fail(QString* error, const QString& message) {
    if (error) {
        *error = message;
    }
    return false;
}

bool parseRewards(const QJsonValue& value, QVector<Reward>* out, QString* error) {
    if (value.isUndefined() || value.isNull()) {
        return true;
    }
    if (!value.isArray()) {
        return fail(error, QStringLiteral("\"rewards\" must be an array"));
    }
    QSet<qint64> serials;
    const QJsonArray array = value.toArray();
    for (qsizetype i = 0; i < array.size(); ++i) {
        const QString where = QStringLiteral("rewards[%1]").arg(i);
        if (!array[i].isObject()) {
            return fail(error, where + QStringLiteral(" must be an object"));
        }
        Reward reward;
        if (!parseReward(array[i].toObject(), where, &reward, error)) {
            return false;
        }
        if (serials.contains(reward.serial)) {
            return fail(error,
                        where + QStringLiteral(".serial %1 is used twice").arg(reward.serial));
        }
        serials.insert(reward.serial);
        out->append(reward);
    }
    return true;
}

bool parseEvent(const QJsonValue& value, QByteArray* out, QString* error) {
    if (value.isUndefined() || value.isNull()) {
        *out = QByteArrayLiteral("{}");
        return true;
    }
    if (!value.isObject()) {
        return fail(error, QStringLiteral("\"event\" must be an object"));
    }
    const QJsonObject obj = value.toObject();
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        const bool known =
            std::any_of(eventKeys().begin(), eventKeys().end(),
                        [&](const EventKey& k) { return it.key() == QLatin1String(k.name); });
        if (!known && !it.key().startsWith('_')) {
            return fail(error, QStringLiteral(
                                   "event has unknown key \"%1\"; the game rejects the whole event")
                                   .arg(it.key()));
        }
    }

    JsonWriter w;
    w.beginObject();
    for (const EventKey& k : eventKeys()) {
        if (!obj.contains(k.name)) {
            continue;
        }
        const QJsonValue v = obj.value(k.name);
        const QString where = QStringLiteral("event.%1").arg(QLatin1String(k.name));
        w.key(QString::fromLatin1(k.name));
        if (k.kind == EventKind::Scalar) {
            qint64 i = 0;
            if (!readInt(v, 0, k.max, &i)) {
                return fail(error, where + QStringLiteral(" must be an integer 0..%1").arg(k.max));
            }
            w.value(i);
            continue;
        }
        if (!v.isArray() || v.toArray().size() > k.maxCount) {
            return fail(
                error,
                where + QStringLiteral(" must be an array of at most %1 entries").arg(k.maxCount));
        }
        w.beginArray();
        for (const QJsonValue& e : v.toArray()) {
            if (k.kind == EventKind::StringArray) {
                const QString s = e.toString();
                bool ascii = std::all_of(s.begin(), s.end(), [](QChar c) {
                    return c.unicode() >= 0x20 && c.unicode() < 0x7f;
                });
                if (!e.isString() || s.size() > k.max || !ascii) {
                    return fail(error,
                                where +
                                    QStringLiteral(
                                        " entries must be ASCII strings of at most %1 characters")
                                        .arg(k.max));
                }
                w.value(s);
            } else {
                qint64 i = 0;
                if (!readInt(e, 0, k.max, &i)) {
                    return fail(error,
                                where +
                                    QStringLiteral(" entries must be integers 0..%1").arg(k.max));
                }
                w.value(i);
            }
        }
        w.endArray();
    }
    w.endObject();

    const auto length = [&](const char* key) { return obj.value(key).toArray().size(); };
    if (length("subjugation_enemy_id") != length("subjugation_exp")) {
        return fail(
            error,
            QStringLiteral(
                "event.subjugation_enemy_id and event.subjugation_exp must have the same length"));
    }
    if (length("weekend_item_item_id") != length("weekend_item_price")) {
        return fail(error, QStringLiteral("event.weekend_item_item_id and event.weekend_item_price "
                                          "must have the same length"));
    }
    *out = w.data();
    return true;
}

} // namespace

bool parseReward(const QJsonObject& obj, const QString& where, Reward* out, QString* error) {
    Reward& reward = *out;
    reward = Reward{};
    if (!readInt(obj.value("serial"), 1, I32Max, &reward.serial)) {
        return fail(error, where + QStringLiteral(".serial must be an integer 1..2147483647"));
    }

    const QJsonValue typeValue = obj.value("type");
    const RewardType* type = nullptr;
    for (const RewardType& t : rewardTypes()) {
        if ((typeValue.isString() && typeValue.toString() == QLatin1String(t.name)) ||
            (typeValue.isDouble() && typeValue.toDouble() == t.id)) {
            type = &t;
        }
    }
    if (!type) {
        return fail(error,
                    where + QStringLiteral(".type must be relief, gacha, parts, weapon or 1..4"));
    }
    reward.type = type->id;

    QSet<QString> allowed = {QStringLiteral("serial"), QStringLiteral("type")};
    for (const FieldSpec& field : type->fields) {
        qint64 v = 0;
        if (!readInt(obj.value(field.name), 0, field.max, &v)) {
            return fail(error, where + QStringLiteral(".%1 must be an integer 0..%2")
                                           .arg(QLatin1String(field.name))
                                           .arg(field.max));
        }
        reward.fields.append({QString::fromLatin1(field.name), v});
        allowed.insert(QString::fromLatin1(field.name));
    }
    if (type->id == 4 && obj.contains("weapon_randomtag")) {
        if (!obj.value("weapon_randomtag").isBool()) {
            return fail(error, where + QStringLiteral(".weapon_randomtag must be true or false"));
        }
        reward.weaponRandomTag = obj.value("weapon_randomtag").toBool();
        allowed.insert(QStringLiteral("weapon_randomtag"));
    }
    if (obj.contains("players")) {
        const QJsonValue players = obj.value("players");
        if (!players.isArray() || players.toArray().isEmpty()) {
            return fail(error,
                        where + QStringLiteral(".players must be a non-empty array of online IDs"));
        }
        for (const QJsonValue& p : players.toArray()) {
            const QString name = p.toString().trimmed().toLower();
            if (!p.isString() || name.isEmpty()) {
                return fail(error,
                            where + QStringLiteral(".players must contain only non-empty strings"));
            }
            if (!reward.players.contains(name)) {
                reward.players.append(name);
            }
        }
        allowed.insert(QStringLiteral("players"));
    }
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        if (!allowed.contains(it.key()) && !it.key().startsWith('_')) {
            return fail(error,
                        where + QStringLiteral(" has field \"%1\", which a %2 reward cannot carry")
                                    .arg(it.key(), QLatin1String(type->name)));
        }
    }
    return true;
}

bool parseEventJson(const QJsonValue& value, QByteArray* out, QString* error) {
    return parseEvent(value, out, error);
}

QJsonObject rewardToJson(const Reward& reward) {
    QJsonObject obj;
    obj.insert(QStringLiteral("serial"), reward.serial);
    for (const RewardType& t : rewardTypes()) {
        if (t.id == reward.type) {
            obj.insert(QStringLiteral("type"), QLatin1String(t.name));
        }
    }
    for (const auto& [name, value] : reward.fields) {
        obj.insert(name, value);
    }
    if (reward.weaponRandomTag) {
        obj.insert(QStringLiteral("weapon_randomtag"), *reward.weaponRandomTag);
    }
    if (!reward.players.isEmpty()) {
        obj.insert(QStringLiteral("players"), QJsonArray::fromStringList(reward.players));
    }
    return obj;
}

std::optional<ServerConfig> loadConfig(const QByteArray& json, QString* error) {
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        fail(error,
             QStringLiteral("config is not a JSON object: %1").arg(parseError.errorString()));
        return std::nullopt;
    }
    const QJsonObject root = doc.object();
    ServerConfig config;

    const QJsonObject listen = root.value("listen").toObject();
    if (listen.contains("address")) {
        if (!config.address.setAddress(listen.value("address").toString())) {
            fail(error, QStringLiteral("listen.address is not an IP address"));
            return std::nullopt;
        }
    }
    if (listen.contains("port")) {
        qint64 port = 0;
        if (!readInt(listen.value("port"), 1, 65535, &port)) {
            fail(error, QStringLiteral("listen.port must be 1..65535"));
            return std::nullopt;
        }
        config.port = static_cast<quint16>(port);
    }
    if (root.contains("base_path")) {
        config.basePath = root.value("base_path").toString();
        while (config.basePath.endsWith('/')) {
            config.basePath.chop(1);
        }
    }
    if (root.contains("database")) {
        config.database = root.value("database").toString();
    }
    if (root.contains("key")) {
        config.key = root.value("key").toString().toLatin1();
        if (config.key.isEmpty() || config.key.size() > 56) {
            fail(error, QStringLiteral("key must be 1..56 characters"));
            return std::nullopt;
        }
    }
    const QJsonObject shadnet = root.value("shadnet").toObject();
    config.shadnetUrl = shadnet.value("url").toString();
    while (config.shadnetUrl.endsWith('/')) {
        config.shadnetUrl.chop(1);
    }
    if (!config.shadnetUrl.isEmpty() && !config.shadnetUrl.startsWith(QLatin1String("http://")) &&
        !config.shadnetUrl.startsWith(QLatin1String("https://"))) {
        fail(error, QStringLiteral("shadnet.url must start with http:// or https://"));
        return std::nullopt;
    }
    if (config.shadnetUrl.isEmpty()) {
        fail(error,
             QStringLiteral(
                 "shadnet.url is required (shadNet's WebAPI, such as http://127.0.0.1:31315)"));
        return std::nullopt;
    }
    const QJsonObject admin = root.value("admin").toObject();
    if (admin.contains("address") &&
        !config.adminAddress.setAddress(admin.value("address").toString())) {
        fail(error, QStringLiteral("admin.address is not an IP address"));
        return std::nullopt;
    }
    if (admin.contains("port")) {
        qint64 port = 0;
        if (!readInt(admin.value("port"), 1, 65535, &port)) {
            fail(error, QStringLiteral("admin.port must be 1..65535"));
            return std::nullopt;
        }
        config.adminPort = static_cast<quint16>(port);
    }
    const QJsonObject log = root.value("log").toObject();
    config.logFile = log.value("file").toString();
    if (log.contains("max_size_mb")) {
        qint64 mb = 0;
        if (!readInt(log.value("max_size_mb"), 1, 4096, &mb)) {
            fail(error, QStringLiteral("log.max_size_mb must be 1..4096"));
            return std::nullopt;
        }
        config.logMaxBytes = mb * 1024 * 1024;
    }
    const QJsonValue boards = root.value("leaderboards");
    if (!boards.isUndefined() && !boards.isObject()) {
        fail(error, QStringLiteral("leaderboards must be an object"));
        return std::nullopt;
    }
    if (boards.toObject().contains("public")) {
        const QJsonValue v = boards.toObject().value("public");
        if (!v.isBool()) {
            fail(error, QStringLiteral("leaderboards.public must be true or false"));
            return std::nullopt;
        }
        config.leaderboardsPublic = v.toBool();
    }
    if (boards.toObject().contains("size")) {
        qint64 n = 0;
        if (!readInt(boards.toObject().value("size"), 1, 1000, &n)) {
            fail(error, QStringLiteral("leaderboards.size must be 1..1000"));
            return std::nullopt;
        }
        config.leaderboardSize = int(n);
    }
    config.adminKey = admin.value("key").toString();
    if (!config.adminKey.isEmpty() && config.adminKey.size() < 8) {
        fail(error, QStringLiteral("admin.key must be at least 8 characters"));
        return std::nullopt;
    }
    if (!parseRewards(root.value("rewards"), &config.rewards, error) ||
        !parseEvent(root.value("event"), &config.eventJson, error)) {
        return std::nullopt;
    }
    return config;
}

QByteArray defaultConfigJson() {
    static const char alphabet[] = "abcdefghijkmnopqrstuvwxyzABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    QByteArray key;
    for (int i = 0; i < 24; ++i) {
        key += alphabet[QRandomGenerator::system()->bounded(int(sizeof(alphabet) - 1))];
    }
    QByteArray json = QByteArrayLiteral(R"({
    "listen": { "address": "0.0.0.0", "port": 8080 },
    "base_path": "/residentevil/revelations/psn2",
    "database": "revelations.sqlite",
    "shadnet": { "url": "http://127.0.0.1:31315" },
    "admin": { "address": "127.0.0.1", "port": 8081, "key": "@KEY@" },
    "log": { "file": "revelations.log", "max_size_mb": 10 },
    "leaderboards": { "public": true, "size": 10 },
    "rewards": [],
    "event": {}
}
)");
    return json.replace("@KEY@", key);
}
