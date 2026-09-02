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
#ifndef CELLBROADCASTGEOFENCE_H
#define CELLBROADCASTGEOFENCE_H

#include <QHash>
#include <QList>
#include <QObject>
#include <QTimer>

class QGeoPositionInfo;
class QGeoPositionInfoSource;

class CellBroadcastGeoFence : public QObject
{
    Q_OBJECT

public:
    explicit CellBroadcastGeoFence(QObject *parent = 0,
                                   QGeoPositionInfoSource *source = 0);

    void check(quint64 alertId, const QString &geometries, qint64 deadline,
               qint64 createdAt);

Q_SIGNALS:
    void resolved(quint64 alertId, bool display, const QString &state);

private Q_SLOTS:
    void positionUpdated(const QGeoPositionInfo &position);
    void positionTimeout();
    void checkDeadlines();

private:
    struct Request {
        QString geometries;
        qint64 deadline;
        qint64 createdAt;
        qint64 startedAt;
    };

    QList<quint64> orderedAlertIds() const;
    void finish(quint64 alertId, bool display, const QString &state);
    void updateSource();

private:
    QGeoPositionInfoSource *m_source;
    QHash<quint64, Request> m_requests;
    QTimer m_deadlineTimer;
};

#endif
