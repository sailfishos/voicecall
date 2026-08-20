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
#include "cellbroadcastgeofence.h"
#include "cellbroadcastgeometry.h"

#include <QDateTime>
#include <QGeoCoordinate>
#include <QGeoPositionInfo>
#include <QGeoPositionInfoSource>
#include <QtGlobal>

#include <algorithm>

CellBroadcastGeoFence::CellBroadcastGeoFence(QObject *parent,
                                             QGeoPositionInfoSource *source)
    : QObject(parent)
    , m_source(source ? source : QGeoPositionInfoSource::createDefaultSource(this))
{
    m_deadlineTimer.setSingleShot(true);
    connect(&m_deadlineTimer, &QTimer::timeout,
            this, &CellBroadcastGeoFence::checkDeadlines);
    if (m_source) {
        m_source->setUpdateInterval(1000);
        connect(m_source, &QGeoPositionInfoSource::positionUpdated,
                this, &CellBroadcastGeoFence::positionUpdated);
        connect(m_source, &QGeoPositionInfoSource::updateTimeout,
                this, &CellBroadcastGeoFence::positionTimeout);
    }
}

void CellBroadcastGeoFence::check(quint64 alertId, const QString &geometries,
                                  qint64 deadline, qint64 createdAt)
{
    if (!alertId || geometries.isEmpty()) {
        return;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (deadline <= now) {
        Q_EMIT resolved(alertId, true, QStringLiteral("timeout"));
        return;
    }
    if (!m_source) {
        Q_EMIT resolved(alertId, true, QStringLiteral("location-unavailable"));
        return;
    }

    const Request previous = m_requests.value(alertId);
    Request request;
    request.geometries = geometries;
    request.deadline = deadline;
    request.createdAt = createdAt;
    request.startedAt = !previous.geometries.isEmpty()
            && previous.geometries == geometries && previous.deadline == deadline
            ? previous.startedAt : now;
    m_requests.insert(alertId, request);
    updateSource();
}

void CellBroadcastGeoFence::positionUpdated(const QGeoPositionInfo &position)
{
    const QGeoCoordinate coordinate = position.coordinate();
    if (!coordinate.isValid()) {
        return;
    }
    if (!position.timestamp().isValid()
            || !position.hasAttribute(QGeoPositionInfo::HorizontalAccuracy)) {
        return;
    }
    const double accuracy = position.attribute(QGeoPositionInfo::HorizontalAccuracy);
    if (!qIsFinite(accuracy) || accuracy < 0.0) {
        return;
    }
    const qint64 positionTime = position.timestamp().toMSecsSinceEpoch();

    const QList<quint64> alertIds = orderedAlertIds();
    for (quint64 alertId : alertIds) {
        if (positionTime < m_requests.value(alertId).startedAt) {
            continue;
        }
        const CellBroadcastGeometry::Evaluation evaluation = CellBroadcastGeometry::evaluate(
                    m_requests.value(alertId).geometries,
                    coordinate.latitude(), coordinate.longitude(), accuracy);
        if (evaluation == CellBroadcastGeometry::Inside) {
            finish(alertId, true, QStringLiteral("inside"));
        } else if (evaluation == CellBroadcastGeometry::Outside) {
            finish(alertId, false, QStringLiteral("outside"));
        } else if (evaluation == CellBroadcastGeometry::Invalid) {
            finish(alertId, true, QStringLiteral("invalid-geometry"));
        }
    }
    updateSource();
}

void CellBroadcastGeoFence::positionTimeout()
{
    const QList<quint64> alertIds = orderedAlertIds();
    for (quint64 alertId : alertIds) {
        finish(alertId, true, QStringLiteral("location-unavailable"));
    }
    updateSource();
}

void CellBroadcastGeoFence::checkDeadlines()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const QList<quint64> alertIds = orderedAlertIds();
    for (quint64 alertId : alertIds) {
        if (m_requests.value(alertId).deadline <= now) {
            finish(alertId, true, QStringLiteral("timeout"));
        }
    }
    updateSource();
}

QList<quint64> CellBroadcastGeoFence::orderedAlertIds() const
{
    QList<quint64> alertIds = m_requests.keys();
    std::sort(alertIds.begin(), alertIds.end(), [this](quint64 first, quint64 second) {
        const Request firstRequest = m_requests.value(first);
        const Request secondRequest = m_requests.value(second);
        return firstRequest.createdAt == secondRequest.createdAt
                ? first < second : firstRequest.createdAt < secondRequest.createdAt;
    });
    return alertIds;
}

void CellBroadcastGeoFence::finish(quint64 alertId, bool display, const QString &state)
{
    if (m_requests.remove(alertId)) {
        Q_EMIT resolved(alertId, display, state);
    }
}

void CellBroadcastGeoFence::updateSource()
{
    if (!m_source) {
        return;
    }
    if (m_requests.isEmpty()) {
        m_deadlineTimer.stop();
        m_source->stopUpdates();
        return;
    }

    qint64 earliestDeadline = -1;
    for (const Request &request : m_requests) {
        if (earliestDeadline < 0 || request.deadline < earliestDeadline) {
            earliestDeadline = request.deadline;
        }
    }
    const int remaining = qMax(1, int(earliestDeadline
                                     - QDateTime::currentMSecsSinceEpoch()));
    m_deadlineTimer.start(remaining);
    m_source->startUpdates();
}
