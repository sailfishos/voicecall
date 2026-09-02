/*
 * This file is a part of the Voice Call Manager project
 *
 * Copyright (C) 2026 Jolla Mobile Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include "cellbroadcaststore.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QSet>

#include <algorithm>

namespace {

const int SchemaVersion = 1;
const int MaximumHistoryPage = 100;
const int DefaultMaximumWaitSeconds = 30;
const int SharedWarningAreaTriggerType = 2;
const qint64 DuplicateWindowMilliseconds = 24LL * 60 * 60 * 1000;

QString currentBootId()
{
    QFile file(QStringLiteral("/proc/sys/kernel/random/boot_id"));
    return file.open(QIODevice::ReadOnly | QIODevice::Text)
            ? QString::fromLatin1(file.readAll()).trimmed() : QString();
}

QString valueString(const QVariantMap &properties, const char *key)
{
    return properties.value(QString::fromLatin1(key)).toString();
}

int valueInt(const QVariantMap &properties, const char *key, int fallback = -1)
{
    const QVariant value = properties.value(QString::fromLatin1(key));
    return value.isValid() ? value.toInt() : fallback;
}

qint64 receivedTime(const QVariantMap &properties)
{
    const QVariant value = properties.value(QStringLiteral("ReceivedAt"));
    return value.isValid() ? value.toLongLong()
                           : QDateTime::currentDateTimeUtc().toMSecsSinceEpoch();
}

QString digest(const QByteArray &value)
{
    return QString::fromLatin1(QCryptographicHash::hash(value, QCryptographicHash::Sha256).toHex());
}

bool updateIsNewer(int candidate, int current)
{
    if (candidate < 0 || current < 0 || candidate == current) {
        return false;
    }
    const int distance = (candidate - current + 16) % 16;
    return distance > 0 && distance <= 8;
}

bool needsGeoCheck(const QVariantMap &properties)
{
    return !valueString(properties, "Geometries").isEmpty();
}

QString effectiveGeometries(const QVariantMap &properties)
{
    const QString prepared = valueString(properties,
                                         "CellBroadcastGeoFenceGeometries");
    return prepared.isEmpty() ? valueString(properties, "Geometries") : prepared;
}

int maximumWaitSeconds(const QVariantMap &properties)
{
    int seconds = valueInt(properties, "MaximumWaitTime", DefaultMaximumWaitSeconds);
    if (seconds <= 0 || seconds == 255) {
        seconds = DefaultMaximumWaitSeconds;
    }
    return qBound(1, seconds, 255);
}

qint64 newGeoFenceDeadline(const QVariantMap &properties, qint64 startTime)
{
    if (startTime <= 0) {
        startTime = receivedTime(properties);
    }
    return startTime + qint64(maximumWaitSeconds(properties)) * 1000;
}

QString broadcastPlmn(const QVariantMap &properties)
{
    const QString classified = valueString(properties, "CellBroadcastPlmn");
    return classified.isEmpty()
            ? valueString(properties, "MobileCountryCode")
              + valueString(properties, "MobileNetworkCode")
            : classified;
}

bool sameBroadcastContext(const QVariantMap &referenced,
                          const QVariantMap &trigger)
{
    const QString referencedModem = valueString(referenced, "ModemPath");
    const QString triggerModem = valueString(trigger, "ModemPath");
    if (!referencedModem.isEmpty() && !triggerModem.isEmpty()
            && referencedModem != triggerModem) {
        return false;
    }

    const QString referencedPlmn = broadcastPlmn(referenced);
    const QString triggerPlmn = broadcastPlmn(trigger);
    return referencedPlmn.isEmpty() || triggerPlmn.isEmpty()
            || referencedPlmn == triggerPlmn;
}

} // namespace

CellBroadcastStore::CellBroadcastStore(const QString &databasePath, QObject *parent)
    : QObject(parent)
    , m_connectionName(QStringLiteral("voicecall-cellbroadcast-%1")
                       .arg(reinterpret_cast<quintptr>(this), 0, 16))
    , m_bootId(currentBootId())
    , m_open(false)
    , m_durable(false)
{
    QString path = databasePath;
    if (path.isEmpty()) {
        const QString directory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                + QStringLiteral("/cellbroadcast");
        if (QDir().mkpath(directory)) {
            path = directory + QStringLiteral("/alerts.sqlite");
        } else {
            m_errorString = QStringLiteral("Unable to create Cell Broadcast storage directory: ")
                    + directory;
        }
    }

    if (!path.isEmpty() && open(path)) {
        m_durable = path != QLatin1String(":memory:");
    }
}

CellBroadcastStore::~CellBroadcastStore()
{
    {
        QSqlDatabase database = QSqlDatabase::database(m_connectionName, false);
        if (database.isValid()) {
            database.close();
        }
    }
    QSqlDatabase::removeDatabase(m_connectionName);
}

bool CellBroadcastStore::isOpen() const
{
    return m_open;
}

bool CellBroadcastStore::isDurable() const
{
    return m_durable;
}

QString CellBroadcastStore::databasePath() const
{
    return m_databasePath;
}

QString CellBroadcastStore::errorString() const
{
    return m_errorString;
}

bool CellBroadcastStore::open(const QString &path)
{
    if (QSqlDatabase::contains(m_connectionName)) {
        {
            QSqlDatabase previous = QSqlDatabase::database(m_connectionName, false);
            previous.close();
        }
        QSqlDatabase::removeDatabase(m_connectionName);
    }

    QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    database.setDatabaseName(path);
    if (!database.open()) {
        setError(database.lastError().text());
        m_open = false;
        return false;
    }

    m_databasePath = path;
    m_open = true;
    QSqlQuery pragma(database);
    if (!pragma.exec(QStringLiteral("PRAGMA foreign_keys = ON"))) {
        setError(pragma.lastError().text());
        database.close();
        m_open = false;
        return false;
    }
    if (!pragma.exec(QStringLiteral("PRAGMA foreign_keys"))
            || !pragma.next() || pragma.value(0).toInt() != 1) {
        const QString error = pragma.lastError().text();
        setError(error.isEmpty()
                 ? QStringLiteral("SQLite foreign key enforcement is unavailable")
                 : error);
        database.close();
        m_open = false;
        return false;
    }
    if (!createSchema() || !reconcileQueue()) {
        database.close();
        m_open = false;
        return false;
    }
    return true;
}

bool CellBroadcastStore::createSchema()
{
    QSqlDatabase database = QSqlDatabase::database(m_connectionName);
    if (!database.transaction()) {
        setError(database.lastError().text());
        return false;
    }

    QSqlQuery query(database);
    if (!query.exec(QStringLiteral("PRAGMA user_version")) || !query.next()) {
        setError(query.lastError().text());
        database.rollback();
        return false;
    }

    bool versionOk = false;
    const int version = query.value(0).toInt(&versionOk);
    if (!versionOk || (version != 0 && version != SchemaVersion)) {
        setError(QStringLiteral("Unsupported Cell Broadcast database schema version"));
        database.rollback();
        return false;
    }
    if (version == 0) {
        if (!query.exec(QStringLiteral(
                            "SELECT COUNT(*) FROM sqlite_master WHERE type='table' "
                            "AND name IN ('cellbroadcast_alerts','cellbroadcast_messages')"))
                || !query.next()) {
            setError(query.lastError().text());
            database.rollback();
            return false;
        }
        if (query.value(0).toInt() != 0) {
            setError(QStringLiteral("Unversioned Cell Broadcast database schema"));
            database.rollback();
            return false;
        }
    }

    const QStringList tableStatements = {
        QStringLiteral("CREATE TABLE IF NOT EXISTS cellbroadcast_alerts ("
                       "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                       "logical_key TEXT NOT NULL UNIQUE, "
                       "category_id TEXT, title TEXT, alert_level TEXT, "
                       "attention_policy TEXT, display_policy TEXT NOT NULL, "
                       "state INTEGER NOT NULL, created_at INTEGER NOT NULL, "
                       "updated_at INTEGER NOT NULL, current_message_id INTEGER, "
                       "geo_state TEXT NOT NULL DEFAULT '', "
                       "acknowledged_at INTEGER, silenced_at INTEGER)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS cellbroadcast_messages ("
                       "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                       "alert_id INTEGER NOT NULL REFERENCES cellbroadcast_alerts(id) ON DELETE CASCADE, "
                       "version_key TEXT NOT NULL UNIQUE, message_identifier INTEGER, "
                       "serial_number INTEGER, geographical_scope INTEGER, "
                       "message_code INTEGER, update_number INTEGER, dcs INTEGER, "
                       "language TEXT, language_role TEXT, page_count INTEGER, "
                       "body TEXT NOT NULL, properties_json TEXT NOT NULL, wac BLOB, "
                       "received_at INTEGER NOT NULL, last_received_at INTEGER NOT NULL, "
                       "receipt_count INTEGER NOT NULL DEFAULT 1, supersedes_id INTEGER, "
                       "boot_id TEXT NOT NULL DEFAULT '')")
    };
    for (const QString &statement : tableStatements) {
        if (!query.exec(statement)) {
            setError(query.lastError().text());
            database.rollback();
            return false;
        }
    }

    const QStringList indexStatements = {
        QStringLiteral("CREATE INDEX IF NOT EXISTS cellbroadcast_alert_state "
                       "ON cellbroadcast_alerts(state, created_at, id)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS cellbroadcast_message_alert "
                       "ON cellbroadcast_messages(alert_id, update_number, id)")
    };
    for (const QString &statement : indexStatements) {
        if (!query.exec(statement)) {
            setError(query.lastError().text());
            database.rollback();
            return false;
        }
    }

    if (version == 0) {
        if (!query.exec(QStringLiteral("PRAGMA user_version = %1").arg(SchemaVersion))) {
            setError(query.lastError().text());
            database.rollback();
            return false;
        }
    }

    if (!database.commit()) {
        setError(query.lastError().text().isEmpty()
                 ? database.lastError().text() : query.lastError().text());
        database.rollback();
        return false;
    }
    return true;
}

bool CellBroadcastStore::reconcileQueue()
{
    QSqlDatabase database = QSqlDatabase::database(m_connectionName);
    if (!pruneSuppressed(database,
                         QDateTime::currentDateTimeUtc().toMSecsSinceEpoch())) {
        return false;
    }

    QSqlQuery query(database);
    if (!query.exec(QStringLiteral("SELECT id FROM cellbroadcast_alerts "
                                   "WHERE state=1 ORDER BY created_at,id"))) {
        setError(query.lastError().text());
        return false;
    }

    bool haveActive = false;
    QList<quint64> extraActive;
    while (query.next()) {
        if (!haveActive) {
            haveActive = true;
        } else {
            extraActive.append(query.value(0).toULongLong());
        }
    }
    for (quint64 id : extraActive) {
        QSqlQuery update(database);
        update.prepare(QStringLiteral("UPDATE cellbroadcast_alerts SET state=0 WHERE id=?"));
        update.addBindValue(id);
        if (!update.exec()) {
            setError(update.lastError().text());
            return false;
        }
    }

    if (!haveActive) {
        QSqlQuery pending(database);
        if (!pending.exec(QStringLiteral("SELECT id FROM cellbroadcast_alerts WHERE state=0 "
                                         "ORDER BY created_at,id LIMIT 1"))) {
            setError(pending.lastError().text());
            return false;
        }
        if (pending.next()) {
            QSqlQuery promote(database);
            promote.prepare(QStringLiteral("UPDATE cellbroadcast_alerts SET state=1 WHERE id=?"));
            promote.addBindValue(pending.value(0));
            if (!promote.exec()) {
                setError(promote.lastError().text());
                return false;
            }
        }
    }
    return true;
}

bool CellBroadcastStore::pruneSuppressed(QSqlDatabase &database, qint64 now,
                                         const QString &preserveLogicalKey)
{
    QSqlQuery query(database);
    query.prepare(QStringLiteral(
                      "DELETE FROM cellbroadcast_alerts WHERE state=? AND "
                      "(?=1 OR logical_key<>?) AND "
                      "COALESCE((SELECT MAX(last_received_at) "
                      "FROM cellbroadcast_messages WHERE alert_id="
                      "cellbroadcast_alerts.id),0)<?"));
    query.addBindValue(Suppressed);
    query.addBindValue(preserveLogicalKey.isEmpty() ? 1 : 0);
    query.addBindValue(preserveLogicalKey);
    query.addBindValue(now - DuplicateWindowMilliseconds);
    if (!query.exec()) {
        setError(query.lastError().text());
        return false;
    }
    return true;
}

QString CellBroadcastStore::logicalKey(const QString &text,
                                       const QVariantMap &properties) const
{
    const int serial = valueInt(properties, "SerialNumber");
    const int scope = valueInt(properties, "GeographicalScope");
    const int messageIdentifier = valueInt(properties, "MessageIdentifier",
                                           valueInt(properties, "Topic"));
    const QString languageRole = valueString(properties, "CellBroadcastLanguageRole");
    QString family;
    if (languageRole == QLatin1String("local")
            || languageRole == QLatin1String("additional")) {
        family = valueString(properties, "CellBroadcastCategory");
    }
    if (family.isEmpty()) {
        family = QString::number(messageIdentifier);
    }

    if (serial < 0) {
        const QByteArray legacy = text.toUtf8() + QByteArray::number(
                    valueInt(properties, "Topic")) + QByteArray::number(receivedTime(properties));
        return QStringLiteral("legacy|") + digest(legacy);
    }

    QString area = broadcastPlmn(properties);
    if (scope == 2) {
        area += QLatin1Char('|') + QString::number(valueInt(properties, "LocationAreaCode"));
    } else if (scope != 1) {
        area += QLatin1Char('|') + QString::number(valueInt(properties, "LocationAreaCode"));
        area += QLatin1Char('|') + QString::number(valueInt(properties, "CellId"));
    }

    return area + QLatin1Char('|') + QString::number(scope) + QLatin1Char('|')
            + family + QLatin1Char('|') + QString::number((serial >> 4) & 0x03ff);
}

QString CellBroadcastStore::versionKey(const QString &key,
                                       const QString &text,
                                       const QVariantMap &properties) const
{
    QByteArray value = key.toUtf8();
    value += '|';
    value += QByteArray::number(valueInt(properties, "MessageIdentifier",
                                         valueInt(properties, "Topic")));
    value += '|';
    value += QByteArray::number(valueInt(properties, "SerialNumber"));
    value += '|';
    value += valueString(properties, "Language").toUtf8();
    value += '|';
    value += text.toUtf8();
    return digest(value);
}

bool CellBroadcastStore::presentationEligible(const QVariantMap &properties) const
{
    if (valueString(properties, "CellBroadcastDisplay") == QLatin1String("none")) {
        return false;
    }
    if (properties.contains(QStringLiteral("CellBroadcastEnabled"))
            && !properties.value(QStringLiteral("CellBroadcastEnabled")).toBool()) {
        return false;
    }

    if (valueString(properties, "CellBroadcastLanguageRole") != QLatin1String("additional")) {
        return true;
    }
    const QString language = valueString(properties, "Language").left(2).toLower();
    return language.isEmpty() || language == QLocale::system().name().left(2).toLower();
}

int CellBroadcastStore::languageScore(const QVariantMap &properties) const
{
    const QString role = valueString(properties, "CellBroadcastLanguageRole");
    const QString language = valueString(properties, "Language").left(2).toLower();
    const QString systemLanguage = QLocale::system().name().left(2).toLower();
    if (role == QLatin1String("additional") && language == systemLanguage) {
        return 3;
    }
    if (role == QLatin1String("local")) {
        return 2;
    }
    return language.isEmpty() ? 1 : 0;
}

CellBroadcastStore::StoreResult CellBroadcastStore::store(
        const QString &text, const QVariantMap &properties)
{
    StoreResult result;
    const bool structuredEmptyMessage = properties.value(
                QStringLiteral("Primary")).toBool()
            || valueString(properties, "CellBroadcastDisplay") == QLatin1String("none")
            || !valueString(properties, "DeviceBasedGeoFencingReferenceList").isEmpty();
    if (!m_open || (text.isEmpty() && !structuredEmptyMessage)) {
        return result;
    }

    const QString body = text.isNull() ? QStringLiteral("") : text;
    const QString key = logicalKey(body, properties);
    const QString messageKey = versionKey(key, body, properties);
    const qint64 timestamp = receivedTime(properties);
    const quint64 activeBefore = activeAlertId();
    QSqlDatabase database = QSqlDatabase::database(m_connectionName);
    if (!database.transaction()) {
        setError(database.lastError().text());
        return result;
    }
    const qint64 pruneTime = QDateTime::currentDateTimeUtc().toMSecsSinceEpoch();
    if (!pruneSuppressed(database, pruneTime,
                         timestamp >= pruneTime - DuplicateWindowMilliseconds
                         ? key : QString())) {
        database.rollback();
        return result;
    }

    quint64 alertId = 0;
    int state = Suppressed;
    quint64 currentMessageId = 0;
    int currentUpdate = -1;
    int currentLanguageScore = -1;
    QString currentGeoState;
    bool currentGeoFenceAttentionRequired = false;
    QString currentMessageBootId;
    QVariantMap currentProperties;
    QSqlQuery existing(database);
    existing.prepare(QStringLiteral("SELECT a.id,a.state,a.current_message_id,m.update_number,"
                                    "m.properties_json,a.geo_state,"
                                    "(SELECT MAX(last_received_at) FROM cellbroadcast_messages "
                                    "WHERE alert_id=a.id),m.boot_id "
                                    "FROM cellbroadcast_alerts a "
                                    "LEFT JOIN cellbroadcast_messages m ON m.id=a.current_message_id "
                                    "WHERE a.logical_key=?"));
    existing.addBindValue(key);
    if (!existing.exec()) {
        setError(existing.lastError().text());
        database.rollback();
        return result;
    }

    bool haveExisting = existing.next();
    if (haveExisting) {
        alertId = existing.value(0).toULongLong();
        state = existing.value(1).toInt();
        currentMessageId = existing.value(2).toULongLong();
        currentUpdate = existing.value(3).isNull() ? -1 : existing.value(3).toInt();
        const QJsonDocument currentDocument = QJsonDocument::fromJson(
                    existing.value(4).toByteArray());
        currentProperties = currentDocument.object().toVariantMap();
        currentLanguageScore = languageScore(currentProperties);
        currentGeoFenceAttentionRequired = currentProperties.value(
                    QStringLiteral("CellBroadcastGeoFenceAttentionRequired")).toBool();
        currentGeoState = existing.value(5).toString();
        const qint64 lastReceivedAt = existing.value(6).toLongLong();
        currentMessageBootId = existing.value(7).toString();
        if (lastReceivedAt > 0
                && timestamp > lastReceivedAt + DuplicateWindowMilliseconds) {
            const QString suffix = QStringLiteral("|archived|") + QString::number(alertId);
            QSqlQuery archiveAlert(database);
            archiveAlert.prepare(QStringLiteral(
                                     "UPDATE cellbroadcast_alerts SET logical_key=logical_key||? "
                                     "WHERE id=?"));
            archiveAlert.addBindValue(suffix);
            archiveAlert.addBindValue(alertId);
            if (!archiveAlert.exec()) {
                setError(archiveAlert.lastError().text());
                database.rollback();
                return result;
            }
            QSqlQuery archiveMessages(database);
            archiveMessages.prepare(QStringLiteral(
                                        "UPDATE cellbroadcast_messages "
                                        "SET version_key=version_key||? WHERE alert_id=?"));
            archiveMessages.addBindValue(suffix);
            archiveMessages.addBindValue(alertId);
            if (!archiveMessages.exec()) {
                setError(archiveMessages.lastError().text());
                database.rollback();
                return result;
            }
            haveExisting = false;
            alertId = 0;
            state = Suppressed;
            currentMessageId = 0;
            currentUpdate = -1;
            currentLanguageScore = -1;
            currentGeoState.clear();
            currentGeoFenceAttentionRequired = false;
            currentMessageBootId.clear();
            currentProperties.clear();
        }
    }

    const bool orderingReset = haveExisting && !m_bootId.isEmpty()
            && currentMessageBootId != m_bootId;
    const int candidateUpdate = valueInt(properties, "UpdateNumber");
    const bool orderingResetForCandidate = orderingReset
            && candidateUpdate != currentUpdate;

    QSqlQuery duplicate(database);
    duplicate.prepare(QStringLiteral("SELECT id,alert_id FROM cellbroadcast_messages "
                                     "WHERE version_key=?"));
    duplicate.addBindValue(messageKey);
    if (!duplicate.exec()) {
        setError(duplicate.lastError().text());
        database.rollback();
        return result;
    }
    if (duplicate.next()) {
        const quint64 duplicateMessageId = duplicate.value(0).toULongLong();
        const quint64 duplicateAlertId = duplicate.value(1).toULongLong();
        const bool historicalVersionIsNewer = duplicateMessageId != currentMessageId
                && (orderingResetForCandidate
                    || updateIsNewer(candidateUpdate, currentUpdate));
        if (historicalVersionIsNewer) {
            QSqlQuery archive(database);
            archive.prepare(QStringLiteral(
                                "UPDATE cellbroadcast_messages SET version_key=version_key||? "
                                "WHERE id=?"));
            archive.addBindValue(QStringLiteral("|reused|")
                                 + QString::number(duplicateMessageId));
            archive.addBindValue(duplicateMessageId);
            if (!archive.exec()) {
                setError(archive.lastError().text());
                database.rollback();
                return result;
            }
        } else {
            result.alertId = duplicateAlertId;
            QSqlQuery update(database);
            update.prepare(QStringLiteral(
                               "UPDATE cellbroadcast_messages SET last_received_at=?, "
                               "receipt_count=receipt_count+1 WHERE id=?"));
            update.addBindValue(timestamp);
            update.addBindValue(duplicateMessageId);
            if (!update.exec()) {
                setError(update.lastError().text());
                database.rollback();
                return result;
            }
            if (!database.commit()) {
                setError(database.lastError().text());
                database.rollback();
                return result;
            }
            result.stored = true;
            result.duplicate = true;
            result.activeAlert = activeAlert();
            return result;
        }
    }

    const bool eligible = presentationEligible(properties);
    if (!haveExisting) {
        state = eligible
                ? (needsGeoCheck(properties)
                   ? AwaitingLocation : (activeBefore ? Pending : Active))
                : Suppressed;
        QSqlQuery insertAlert(database);
        insertAlert.prepare(QStringLiteral("INSERT INTO cellbroadcast_alerts("
                                           "logical_key,category_id,title,alert_level,"
                                           "attention_policy,display_policy,state,created_at,updated_at,"
                                           "geo_state) VALUES(?,?,?,?,?,?,?,?,?,?)"));
        insertAlert.addBindValue(key);
        insertAlert.addBindValue(valueString(properties, "CellBroadcastCategory"));
        insertAlert.addBindValue(valueString(properties, "CellBroadcastTitle"));
        insertAlert.addBindValue(valueString(properties, "CellBroadcastAlertLevel"));
        insertAlert.addBindValue(valueString(properties, "CellBroadcastAttentionPolicy"));
        insertAlert.addBindValue(valueString(properties, "CellBroadcastDisplay"));
        insertAlert.addBindValue(state);
        insertAlert.addBindValue(timestamp);
        insertAlert.addBindValue(timestamp);
        insertAlert.addBindValue(state == AwaitingLocation
                                 ? QStringLiteral("checking") : QStringLiteral(""));
        if (!insertAlert.exec()) {
            setError(insertAlert.lastError().text());
            database.rollback();
            return result;
        }
        alertId = insertAlert.lastInsertId().toULongLong();
    }

    const int candidateLanguageScore = languageScore(properties);
    const bool firstMessage = currentMessageId == 0;
    const bool newer = orderingResetForCandidate
            || updateIsNewer(candidateUpdate, currentUpdate);
    const bool preferredLanguage = candidateUpdate == currentUpdate
            && candidateLanguageScore > currentLanguageScore;
    QVariantMap jsonProperties(properties);
    const QString currentRawGeometry = valueString(currentProperties, "Geometries");
    QString candidateRawGeometry = valueString(jsonProperties, "Geometries");
    const QString currentGeometry = effectiveGeometries(currentProperties);
    if (preferredLanguage && candidateRawGeometry.isEmpty()
            && !currentRawGeometry.isEmpty()) {
        jsonProperties.insert(QStringLiteral("Geometries"),
                              currentRawGeometry);
        candidateRawGeometry = currentRawGeometry;
    }
    if (preferredLanguage && candidateRawGeometry == currentRawGeometry
            && !currentGeometry.isEmpty()) {
        jsonProperties.insert(QStringLiteral("CellBroadcastGeoFenceGeometries"),
                              currentGeometry);
        const char *preservedKeys[] = {
            "MaximumWaitTime",
            "CellBroadcastGeoFenceDeadline",
            "CellBroadcastGeoFenceAttentionRequired"
        };
        for (const char *keyName : preservedKeys) {
            const QString key = QString::fromLatin1(keyName);
            if (currentProperties.contains(key)) {
                jsonProperties.insert(key, currentProperties.value(key));
            }
        }
    }
    const bool geometryChanged = preferredLanguage
            && candidateRawGeometry != currentRawGeometry;
    const bool candidateSelected = firstMessage
            || ((newer || preferredLanguage)
                && (eligible || (state != Active && state != Pending
                                 && state != AwaitingLocation)));
    const bool firstPresentable = state == Suppressed && currentGeoState.isEmpty();
    const bool candidateNeedsGeoCheck = candidateSelected && eligible
            && needsGeoCheck(jsonProperties)
            && (firstMessage || newer || state == AwaitingLocation
                || (preferredLanguage && (firstPresentable || geometryChanged)));
    const bool attentionRequired = firstMessage || newer || firstPresentable;
    const bool geoFenceAttentionRequired = attentionRequired
            || ((state != Active && state != Acknowledged)
                && currentGeoFenceAttentionRequired);

    if (needsGeoCheck(jsonProperties)) {
        if (valueString(jsonProperties, "CellBroadcastGeoFenceGeometries").isEmpty()) {
            jsonProperties.insert(QStringLiteral("CellBroadcastGeoFenceGeometries"),
                                  valueString(jsonProperties, "Geometries"));
        }
        if (jsonProperties.value(
                    QStringLiteral("CellBroadcastGeoFenceDeadline")).toLongLong() <= 0) {
            jsonProperties.insert(QStringLiteral("CellBroadcastGeoFenceDeadline"),
                                  newGeoFenceDeadline(jsonProperties, timestamp));
        }
        if (candidateNeedsGeoCheck
                || (preferredLanguage && currentGeoFenceAttentionRequired)) {
            jsonProperties.insert(QStringLiteral("CellBroadcastGeoFenceAttentionRequired"),
                                  geoFenceAttentionRequired);
        }
    }
    const QByteArray wac = jsonProperties.take(QStringLiteral("WarningAreaCoordinates")).toByteArray();
    const QString propertiesJson = QString::fromUtf8(
                QJsonDocument::fromVariant(jsonProperties).toJson(QJsonDocument::Compact));
    QSqlQuery insertMessage(database);
    insertMessage.prepare(QStringLiteral("INSERT INTO cellbroadcast_messages("
                                         "alert_id,version_key,message_identifier,serial_number,"
                                         "geographical_scope,message_code,update_number,dcs,language,"
                                         "language_role,page_count,body,properties_json,wac,received_at,"
                                         "last_received_at,supersedes_id,boot_id) "
                                         "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)"));
    insertMessage.addBindValue(alertId);
    insertMessage.addBindValue(messageKey);
    insertMessage.addBindValue(valueInt(properties, "MessageIdentifier",
                                        valueInt(properties, "Topic")));
    insertMessage.addBindValue(valueInt(properties, "SerialNumber"));
    insertMessage.addBindValue(valueInt(properties, "GeographicalScope"));
    insertMessage.addBindValue(valueInt(properties, "MessageCode"));
    insertMessage.addBindValue(valueInt(properties, "UpdateNumber"));
    insertMessage.addBindValue(valueInt(properties, "DataCodingScheme"));
    insertMessage.addBindValue(valueString(properties, "Language"));
    insertMessage.addBindValue(valueString(properties, "CellBroadcastLanguageRole"));
    insertMessage.addBindValue(valueInt(properties, "PageCount", 1));
    insertMessage.addBindValue(body);
    insertMessage.addBindValue(propertiesJson);
    insertMessage.addBindValue(wac);
    insertMessage.addBindValue(timestamp);
    insertMessage.addBindValue(timestamp);
    insertMessage.addBindValue(currentMessageId ? QVariant(currentMessageId) : QVariant());
    insertMessage.addBindValue(orderingReset && !orderingResetForCandidate
                               ? currentMessageBootId : m_bootId);
    if (!insertMessage.exec()) {
        setError(insertMessage.lastError().text());
        database.rollback();
        return result;
    }
    const quint64 messageId = insertMessage.lastInsertId().toULongLong();

    if (candidateSelected) {
        if (candidateNeedsGeoCheck) {
            state = AwaitingLocation;
        } else if (state == Suppressed && eligible
                   && (!needsGeoCheck(jsonProperties) || currentGeoState.isEmpty())) {
            state = activeBefore ? Pending : Active;
        }
        if (state == Acknowledged && newer && eligible) {
            state = activeBefore ? Pending : Active;
        }
        QSqlQuery updateAlert(database);
        updateAlert.prepare(QStringLiteral("UPDATE cellbroadcast_alerts SET current_message_id=?, "
                                           "category_id=?,title=?,alert_level=?,attention_policy=?,"
                                           "display_policy=?,state=?,geo_state=?,updated_at=?,"
                                           "silenced_at=CASE WHEN ? THEN NULL ELSE silenced_at END,"
                                           "acknowledged_at=CASE WHEN ? THEN NULL ELSE acknowledged_at END "
                                           "WHERE id=?"));
        updateAlert.addBindValue(messageId);
        updateAlert.addBindValue(valueString(properties, "CellBroadcastCategory"));
        updateAlert.addBindValue(valueString(properties, "CellBroadcastTitle"));
        updateAlert.addBindValue(valueString(properties, "CellBroadcastAlertLevel"));
        updateAlert.addBindValue(valueString(properties, "CellBroadcastAttentionPolicy"));
        updateAlert.addBindValue(valueString(properties, "CellBroadcastDisplay"));
        updateAlert.addBindValue(state);
        updateAlert.addBindValue(state == AwaitingLocation
                                 ? QStringLiteral("checking")
                                 : (preferredLanguage && !geometryChanged
                                    ? currentGeoState : QStringLiteral("")));
        updateAlert.addBindValue(timestamp);
        updateAlert.addBindValue(newer);
        updateAlert.addBindValue(newer);
        updateAlert.addBindValue(alertId);
        if (!updateAlert.exec()) {
            setError(updateAlert.lastError().text());
            database.rollback();
            return result;
        }
    }

    if (!database.commit()) {
        setError(database.lastError().text());
        database.rollback();
        return result;
    }

    const quint64 activeAfter = activeAlertId();
    result.alertId = alertId;
    result.stored = true;
    result.needsGeoCheck = candidateNeedsGeoCheck;
    result.presentationChanged = candidateSelected && !candidateNeedsGeoCheck
            && (state == Active || state == Pending);
    result.requestAttention = candidateSelected && activeAfter == alertId
            && !candidateNeedsGeoCheck && attentionRequired;
    result.activeChanged = activeBefore != activeAfter
            || (candidateSelected && state == Active && activeAfter == alertId);
    result.activeAlert = activeAlert();
    return result;
}

quint64 CellBroadcastStore::activeAlertId() const
{
    if (!m_open) {
        return 0;
    }
    QSqlQuery query(QSqlDatabase::database(m_connectionName));
    if (!query.exec(QStringLiteral("SELECT id FROM cellbroadcast_alerts WHERE state=1 "
                                   "ORDER BY created_at,id LIMIT 1")) || !query.next()) {
        return 0;
    }
    return query.value(0).toULongLong();
}

QVariantMap CellBroadcastStore::readAlert(quint64 id) const
{
    QVariantMap result;
    if (!id || !m_open) {
        return result;
    }

    QSqlQuery query(QSqlDatabase::database(m_connectionName));
    query.prepare(QStringLiteral("SELECT a.id,a.category_id,a.title,a.alert_level,"
                                 "a.attention_policy,a.display_policy,a.state,a.created_at,"
                                 "a.updated_at,a.geo_state,a.acknowledged_at,a.silenced_at,"
                                 "m.id,m.body,m.message_identifier,"
                                 "m.serial_number,m.geographical_scope,m.message_code,"
                                 "m.update_number,m.dcs,m.language,m.language_role,m.page_count,"
                                 "m.properties_json,m.wac,m.received_at,m.last_received_at,"
                                 "m.receipt_count,m.supersedes_id "
                                 "FROM cellbroadcast_alerts a JOIN cellbroadcast_messages m "
                                 "ON m.id=a.current_message_id WHERE a.id=?"));
    query.addBindValue(id);
    if (!query.exec() || !query.next()) {
        if (query.lastError().isValid()) {
            setError(query.lastError().text());
        }
        return result;
    }

    const QJsonDocument document = QJsonDocument::fromJson(query.value(23).toByteArray());
    result = document.object().toVariantMap();
    result.insert(QStringLiteral("RecordId"), query.value(0));
    result.insert(QStringLiteral("CellBroadcastCategory"), query.value(1));
    result.insert(QStringLiteral("CellBroadcastTitle"), query.value(2));
    result.insert(QStringLiteral("CellBroadcastAlertLevel"), query.value(3));
    result.insert(QStringLiteral("CellBroadcastAttentionPolicy"), query.value(4));
    result.insert(QStringLiteral("CellBroadcastDisplay"), query.value(5));
    result.insert(QStringLiteral("State"), query.value(6));
    result.insert(QStringLiteral("CreatedAt"), query.value(7));
    result.insert(QStringLiteral("UpdatedAt"), query.value(8));
    result.insert(QStringLiteral("GeoState"), query.value(9));
    result.insert(QStringLiteral("AcknowledgedAt"), query.value(10));
    result.insert(QStringLiteral("SilencedAt"), query.value(11));
    result.insert(QStringLiteral("MessageRecordId"), query.value(12));
    result.insert(QStringLiteral("Text"), query.value(13));
    result.insert(QStringLiteral("MessageIdentifier"), query.value(14));
    result.insert(QStringLiteral("SerialNumber"), query.value(15));
    result.insert(QStringLiteral("GeographicalScope"), query.value(16));
    result.insert(QStringLiteral("MessageCode"), query.value(17));
    result.insert(QStringLiteral("UpdateNumber"), query.value(18));
    result.insert(QStringLiteral("DataCodingScheme"), query.value(19));
    result.insert(QStringLiteral("Language"), query.value(20));
    result.insert(QStringLiteral("CellBroadcastLanguageRole"), query.value(21));
    result.insert(QStringLiteral("PageCount"), query.value(22));
    result.insert(QStringLiteral("WarningAreaCoordinates"), query.value(24));
    result.insert(QStringLiteral("ReceivedAt"), query.value(25));
    result.insert(QStringLiteral("LastReceivedAt"), query.value(26));
    result.insert(QStringLiteral("ReceiptCount"), query.value(27));
    result.insert(QStringLiteral("SupersedesMessageId"), query.value(28));
    return result;
}

QVariantMap CellBroadcastStore::activeAlert() const
{
    return readAlert(activeAlertId());
}

QVariantMap CellBroadcastStore::alert(quint64 id) const
{
    return readAlert(id);
}

QVariantList CellBroadcastStore::alertHistory(quint64 beforeId, int limit) const
{
    QVariantList result;
    if (!m_open) {
        return result;
    }
    limit = qBound(1, limit, MaximumHistoryPage);
    QSqlQuery query(QSqlDatabase::database(m_connectionName));
    if (beforeId) {
        query.prepare(QStringLiteral("SELECT id FROM cellbroadcast_alerts "
                                     "WHERE state IN (0,1,2) AND id<? "
                                     "ORDER BY id DESC LIMIT ?"));
        query.addBindValue(beforeId);
    } else {
        query.prepare(QStringLiteral("SELECT id FROM cellbroadcast_alerts "
                                     "WHERE state IN (0,1,2) ORDER BY id DESC LIMIT ?"));
    }
    query.addBindValue(limit);
    if (!query.exec()) {
        setError(query.lastError().text());
        return result;
    }
    while (query.next()) {
        result.append(readAlert(query.value(0).toULongLong()));
    }
    return result;
}

bool CellBroadcastStore::acknowledge(quint64 id)
{
    if (!m_open || !id) {
        return false;
    }
    QSqlDatabase database = QSqlDatabase::database(m_connectionName);
    if (!database.transaction()) {
        setError(database.lastError().text());
        return false;
    }
    QSqlQuery acknowledge(database);
    acknowledge.prepare(QStringLiteral("UPDATE cellbroadcast_alerts SET state=2,updated_at=?,"
                                       "acknowledged_at=? "
                                       "WHERE id=? AND state=1"));
    const qint64 timestamp = QDateTime::currentDateTimeUtc().toMSecsSinceEpoch();
    acknowledge.addBindValue(timestamp);
    acknowledge.addBindValue(timestamp);
    acknowledge.addBindValue(id);
    if (!acknowledge.exec() || acknowledge.numRowsAffected() != 1) {
        if (acknowledge.lastError().isValid()) {
            setError(acknowledge.lastError().text());
        }
        database.rollback();
        return false;
    }
    QSqlQuery next(database);
    if (!next.exec(QStringLiteral("SELECT id FROM cellbroadcast_alerts WHERE state=0 "
                                  "ORDER BY created_at,id LIMIT 1"))) {
        setError(next.lastError().text());
        database.rollback();
        return false;
    }
    if (next.next()) {
        QSqlQuery promote(database);
        promote.prepare(QStringLiteral("UPDATE cellbroadcast_alerts SET state=1 WHERE id=?"));
        promote.addBindValue(next.value(0));
        if (!promote.exec()) {
            setError(promote.lastError().text());
            database.rollback();
            return false;
        }
    }
    if (!database.commit()) {
        setError(database.lastError().text());
        database.rollback();
        return false;
    }
    return true;
}

bool CellBroadcastStore::silence(quint64 id)
{
    if (!m_open || !id) {
        return false;
    }
    QSqlQuery query(QSqlDatabase::database(m_connectionName));
    query.prepare(QStringLiteral("UPDATE cellbroadcast_alerts SET updated_at=?,silenced_at=? "
                                 "WHERE id=? AND state=1"));
    const qint64 timestamp = QDateTime::currentDateTimeUtc().toMSecsSinceEpoch();
    query.addBindValue(timestamp);
    query.addBindValue(timestamp);
    query.addBindValue(id);
    if (!query.exec()) {
        setError(query.lastError().text());
        return false;
    }
    return query.numRowsAffected() == 1;
}

CellBroadcastStore::StoreResult CellBroadcastStore::resolveGeoFence(
        quint64 id, bool display, const QString &geoState)
{
    StoreResult result;
    if (!m_open || !id) {
        return result;
    }

    const quint64 activeBefore = activeAlertId();
    QSqlDatabase database = QSqlDatabase::database(m_connectionName);
    if (!database.transaction()) {
        setError(database.lastError().text());
        return result;
    }

    QSqlQuery current(database);
    current.prepare(QStringLiteral(
                        "SELECT a.state,m.properties_json FROM cellbroadcast_alerts a "
                        "JOIN cellbroadcast_messages m ON m.id=a.current_message_id "
                        "WHERE a.id=?"));
    current.addBindValue(id);
    if (!current.exec() || !current.next()) {
        setError(current.lastError().text());
        database.rollback();
        return result;
    }
    const int oldState = current.value(0).toInt();
    const QVariantMap currentProperties = QJsonDocument::fromJson(
                current.value(1).toByteArray()).object().toVariantMap();
    const bool attentionRequired = currentProperties.value(
                QStringLiteral("CellBroadcastGeoFenceAttentionRequired")).toBool();
    if (oldState == Acknowledged) {
        database.rollback();
        return result;
    }

    QSqlQuery active(database);
    active.prepare(QStringLiteral("SELECT id FROM cellbroadcast_alerts "
                                  "WHERE state=1 AND id<>? LIMIT 1"));
    active.addBindValue(id);
    if (!active.exec()) {
        setError(active.lastError().text());
        database.rollback();
        return result;
    }
    const bool otherActive = active.next();
    const int state = display ? (otherActive ? Pending : Active) : Suppressed;
    QSqlQuery update(database);
    update.prepare(QStringLiteral("UPDATE cellbroadcast_alerts SET state=?,geo_state=?,"
                                  "updated_at=? WHERE id=?"));
    update.addBindValue(state);
    update.addBindValue(geoState);
    update.addBindValue(QDateTime::currentDateTimeUtc().toMSecsSinceEpoch());
    update.addBindValue(id);
    if (!update.exec()) {
        setError(update.lastError().text());
        database.rollback();
        return result;
    }

    if (state != Active && !otherActive) {
        QSqlQuery next(database);
        if (!next.exec(QStringLiteral("SELECT id FROM cellbroadcast_alerts WHERE state=0 "
                                      "ORDER BY created_at,id LIMIT 1"))) {
            setError(next.lastError().text());
            database.rollback();
            return result;
        }
        if (next.next()) {
            QSqlQuery promote(database);
            promote.prepare(QStringLiteral("UPDATE cellbroadcast_alerts SET state=1 WHERE id=?"));
            promote.addBindValue(next.value(0));
            if (!promote.exec()) {
                setError(promote.lastError().text());
                database.rollback();
                return result;
            }
        }
    }

    if (!database.commit()) {
        setError(database.lastError().text());
        database.rollback();
        return result;
    }
    const quint64 activeAfter = activeAlertId();
    result.alertId = id;
    result.stored = true;
    result.presentationChanged = display;
    result.requestAttention = activeAfter && activeAfter != activeBefore
            && (activeAfter != id || attentionRequired);
    result.activeChanged = activeAfter != activeBefore;
    result.activeAlert = activeAlert();
    return result;
}

QVariantList CellBroadcastStore::pendingGeoFenceAlerts() const
{
    QVariantList result;
    if (!m_open) {
        return result;
    }
    QSqlQuery query(QSqlDatabase::database(m_connectionName));
    if (!query.exec(QStringLiteral("SELECT id FROM cellbroadcast_alerts WHERE state=4 "
                                   "ORDER BY created_at,id"))) {
        setError(query.lastError().text());
        return result;
    }
    while (query.next()) {
        result.append(readAlert(query.value(0).toULongLong()));
    }
    return result;
}

QVariantList CellBroadcastStore::geoFenceAlerts(
        const QString &referenceList, const QVariantMap &triggerProperties) const
{
    QVariantList result;
    if (!m_open || referenceList.isEmpty()) {
        return result;
    }

    const qint64 cutoff = QDateTime::currentDateTimeUtc().addDays(-1).toMSecsSinceEpoch();
    const QStringList references = referenceList.split(QLatin1Char(';'),
                                                       QString::SkipEmptyParts);
    QSet<quint64> alertIds;
    for (const QString &reference : references) {
        const QStringList identity = reference.split(QLatin1Char(','));
        if (identity.count() != 2) {
            continue;
        }
        bool messageIdentifierOk = false;
        bool serialNumberOk = false;
        const int messageIdentifier = identity.at(0).toInt(&messageIdentifierOk);
        const int serialNumber = identity.at(1).toInt(&serialNumberOk);
        if (!messageIdentifierOk || !serialNumberOk) {
            continue;
        }

        QSqlQuery query(QSqlDatabase::database(m_connectionName));
        query.prepare(QStringLiteral("SELECT DISTINCT a.id,m.properties_json "
                                     "FROM cellbroadcast_alerts a "
                                     "JOIN cellbroadcast_messages cm ON cm.id=a.current_message_id "
                                     "JOIN cellbroadcast_messages m ON m.alert_id=a.id "
                                     "WHERE a.state IN (0,3,4) AND m.message_identifier=? "
                                     "AND m.serial_number=? AND m.last_received_at>=? "
                                     "AND m.update_number=cm.update_number"));
        query.addBindValue(messageIdentifier);
        query.addBindValue(serialNumber);
        query.addBindValue(cutoff);
        if (!query.exec()) {
            setError(query.lastError().text());
            return result;
        }
        while (query.next()) {
            const QVariantMap referencedProperties = QJsonDocument::fromJson(
                        query.value(1).toByteArray()).object().toVariantMap();
            if (sameBroadcastContext(referencedProperties, triggerProperties)) {
                alertIds.insert(query.value(0).toULongLong());
            }
        }
    }

    for (quint64 alertId : alertIds) {
        const QVariantMap alert = readAlert(alertId);
        if (!valueString(alert, "Geometries").isEmpty()) {
            result.append(alert);
        }
    }
    std::sort(result.begin(), result.end(), [](const QVariant &first, const QVariant &second) {
        const QVariantMap firstAlert = first.toMap();
        const QVariantMap secondAlert = second.toMap();
        const qint64 firstCreated = firstAlert.value(QStringLiteral("CreatedAt")).toLongLong();
        const qint64 secondCreated = secondAlert.value(QStringLiteral("CreatedAt")).toLongLong();
        return firstCreated == secondCreated
                ? firstAlert.value(QStringLiteral("RecordId")).toULongLong()
                  < secondAlert.value(QStringLiteral("RecordId")).toULongLong()
                : firstCreated < secondCreated;
    });
    return result;
}

QVariantList CellBroadcastStore::prepareGeoFenceTrigger(
        const QString &referenceList, int triggerType, qint64 receivedAt,
        const QVariantMap &triggerProperties)
{
    const QVariantList referenced = geoFenceAlerts(referenceList, triggerProperties);
    if (referenced.isEmpty()) {
        return QVariantList();
    }

    QStringList commonAreas;
    int maximumWait = 0;
    for (const QVariant &value : referenced) {
        const QVariantMap alert = value.toMap();
        maximumWait = qMax(maximumWait, maximumWaitSeconds(alert));
        if (triggerType == SharedWarningAreaTriggerType) {
            const QStringList areas = valueString(alert, "Geometries").split(
                        QLatin1Char(';'), QString::SkipEmptyParts);
            for (const QString &area : areas) {
                if (!commonAreas.contains(area)) {
                    commonAreas.append(area);
                }
            }
        }
    }

    if (receivedAt <= 0) {
        receivedAt = QDateTime::currentDateTimeUtc().toMSecsSinceEpoch();
    }
    const qint64 deadline = receivedAt + qint64(maximumWait) * 1000;
    QSqlDatabase database = QSqlDatabase::database(m_connectionName);
    if (!database.transaction()) {
        setError(database.lastError().text());
        return QVariantList();
    }

    QList<quint64> preparedIds;
    for (const QVariant &value : referenced) {
        const QVariantMap alert = value.toMap();
        const quint64 alertId = alert.value(QStringLiteral("RecordId")).toULongLong();
        const quint64 messageId = alert.value(QStringLiteral("MessageRecordId")).toULongLong();
        const QString geometries = !commonAreas.isEmpty()
                ? commonAreas.join(QStringLiteral(";"))
                : valueString(alert, "Geometries");

        QSqlQuery selectProperties(database);
        selectProperties.prepare(QStringLiteral(
                                     "SELECT properties_json FROM cellbroadcast_messages "
                                     "WHERE id=? AND alert_id=?"));
        selectProperties.addBindValue(messageId);
        selectProperties.addBindValue(alertId);
        if (!selectProperties.exec() || !selectProperties.next()) {
            setError(selectProperties.lastError().text());
            database.rollback();
            return QVariantList();
        }

        QVariantMap properties = QJsonDocument::fromJson(
                    selectProperties.value(0).toByteArray()).object().toVariantMap();
        qint64 alertDeadline = deadline;
        if (alert.value(QStringLiteral("State")).toInt() == AwaitingLocation) {
            const qint64 existingDeadline = alert.value(
                        QStringLiteral("CellBroadcastGeoFenceDeadline")).toLongLong();
            if (existingDeadline > 0) {
                alertDeadline = existingDeadline;
            }
        }
        properties.insert(QStringLiteral("CellBroadcastGeoFenceGeometries"), geometries);
        properties.insert(QStringLiteral("CellBroadcastGeoFenceDeadline"), alertDeadline);

        QSqlQuery updateMessage(database);
        updateMessage.prepare(QStringLiteral(
                                  "UPDATE cellbroadcast_messages SET properties_json=? "
                                  "WHERE id=? AND alert_id=?"));
        updateMessage.addBindValue(QString::fromUtf8(
                                      QJsonDocument::fromVariant(properties)
                                      .toJson(QJsonDocument::Compact)));
        updateMessage.addBindValue(messageId);
        updateMessage.addBindValue(alertId);
        if (!updateMessage.exec() || updateMessage.numRowsAffected() != 1) {
            setError(updateMessage.lastError().text());
            database.rollback();
            return QVariantList();
        }

        QSqlQuery updateAlert(database);
        updateAlert.prepare(QStringLiteral(
                                "UPDATE cellbroadcast_alerts SET state=4,geo_state='checking',"
                                "updated_at=? WHERE id=? AND state IN (0,3,4)"));
        updateAlert.addBindValue(receivedAt);
        updateAlert.addBindValue(alertId);
        if (!updateAlert.exec() || updateAlert.numRowsAffected() != 1) {
            setError(updateAlert.lastError().text());
            database.rollback();
            return QVariantList();
        }
        preparedIds.append(alertId);
    }

    if (!database.commit()) {
        setError(database.lastError().text());
        database.rollback();
        return QVariantList();
    }

    QVariantList prepared;
    for (quint64 alertId : preparedIds) {
        prepared.append(readAlert(alertId));
    }
    return prepared;
}

bool CellBroadcastStore::remove(quint64 id)
{
    if (!m_open || !id) {
        return false;
    }
    QSqlQuery query(QSqlDatabase::database(m_connectionName));
    query.prepare(QStringLiteral("DELETE FROM cellbroadcast_alerts WHERE id=? AND state=2"));
    query.addBindValue(id);
    return query.exec() && query.numRowsAffected() == 1;
}

int CellBroadcastStore::clearHistory()
{
    if (!m_open) {
        return 0;
    }
    QSqlQuery query(QSqlDatabase::database(m_connectionName));
    if (!query.exec(QStringLiteral("DELETE FROM cellbroadcast_alerts WHERE state=2"))) {
        setError(query.lastError().text());
        return 0;
    }
    return query.numRowsAffected();
}

void CellBroadcastStore::setError(const QString &errorString) const
{
    m_errorString = errorString;
}
