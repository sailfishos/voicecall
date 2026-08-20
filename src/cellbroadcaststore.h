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
#ifndef CELLBROADCASTSTORE_H
#define CELLBROADCASTSTORE_H

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

class QSqlDatabase;

class CellBroadcastStore : public QObject
{
public:
    struct StoreResult {
        quint64 alertId = 0;
        bool stored = false;
        bool duplicate = false;
        bool activeChanged = false;
        bool requestAttention = false;
        bool needsGeoCheck = false;
        QVariantMap activeAlert;
    };

    explicit CellBroadcastStore(const QString &databasePath = QString(),
                                QObject *parent = 0);
    ~CellBroadcastStore();

    bool isOpen() const;
    bool isDurable() const;
    QString databasePath() const;
    QString errorString() const;

    StoreResult store(const QString &text, const QVariantMap &properties);
    QVariantMap activeAlert() const;
    QVariantMap alert(quint64 id) const;
    QVariantList alertHistory(quint64 beforeId, int limit) const;
    bool acknowledge(quint64 id);
    bool silence(quint64 id);
    StoreResult resolveGeoFence(quint64 id, bool display, const QString &geoState);
    QVariantList pendingGeoFenceAlerts() const;
    QVariantList geoFenceAlerts(
            const QString &referenceList,
            const QVariantMap &triggerProperties = QVariantMap()) const;
    QVariantList prepareGeoFenceTrigger(const QString &referenceList,
                                        int triggerType,
                                        qint64 receivedAt,
                                        const QVariantMap &triggerProperties = QVariantMap());
    bool remove(quint64 id);
    int clearHistory();

private:
    enum AlertState {
        Pending = 0,
        Active = 1,
        Acknowledged = 2,
        Suppressed = 3,
        AwaitingLocation = 4
    };

    bool open(const QString &path);
    bool createSchema();
    bool reconcileQueue();
    bool pruneSuppressed(QSqlDatabase &database, qint64 now,
                         const QString &preserveLogicalKey = QString());
    QString logicalKey(const QString &text, const QVariantMap &properties) const;
    QString versionKey(const QString &logicalKey,
                       const QString &text,
                       const QVariantMap &properties) const;
    bool presentationEligible(const QVariantMap &properties) const;
    int languageScore(const QVariantMap &properties) const;
    QVariantMap readAlert(quint64 id) const;
    quint64 activeAlertId() const;
    void setError(const QString &errorString) const;

private:
    QString m_connectionName;
    QString m_databasePath;
    QString m_bootId;
    bool m_open;
    bool m_durable;
    mutable QString m_errorString;
};

#endif
