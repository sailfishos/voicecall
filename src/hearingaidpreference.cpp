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
#include "hearingaidpreference.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>

namespace {
const char HearingAidEnabledKey[] = "/sailfish/audio/hearing_aid/enabled";
const char RouteManagerService[] = "org.nemomobile.Route.Manager";
const char RouteManagerPath[] = "/org/nemomobile/Route/Manager";
const char RouteManagerInterface[] = "org.nemomobile.Route.Manager";
const int RetryInterval = 2000;
}

HearingAidPreference::HearingAidPreference(QObject *parent)
    : QObject(parent)
    , m_enabled(QString::fromLatin1(HearingAidEnabledKey), this)
    , m_pending(false)
    , m_dirty(false)
{
    QDBusServiceWatcher *service = new QDBusServiceWatcher(QString::fromLatin1(RouteManagerService),
            QDBusConnection::systemBus(), QDBusServiceWatcher::WatchForOwnerChange, this);
    m_retry.setSingleShot(true);
    m_retry.setInterval(RetryInterval);
    connect(&m_enabled, &MDConfItem::valueChanged, this, &HearingAidPreference::request);
    connect(service, &QDBusServiceWatcher::serviceOwnerChanged, this,
            [this](const QString &, const QString &, const QString &owner) {
        if (!owner.isEmpty())
            request();
    });
    connect(&m_retry, &QTimer::timeout, this, &HearingAidPreference::request);
    request();
}

void HearingAidPreference::request()
{
    m_retry.stop();
    if (m_pending)
        m_dirty = true;
    else
        apply();
}

void HearingAidPreference::apply()
{
    m_dirty = false;
    m_pending = true;
    const bool enabled = m_enabled.value(false).toBool();
    QDBusMessage message = QDBusMessage::createMethodCall(
                QString::fromLatin1(RouteManagerService),
                QString::fromLatin1(RouteManagerPath),
                QString::fromLatin1(RouteManagerInterface),
                enabled ? QStringLiteral("Enable") : QStringLiteral("Disable"));
    message << QStringLiteral("hearing_aid");

    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(
                QDBusConnection::systemBus().asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this](QDBusPendingCallWatcher *call) {
        QDBusPendingReply<> reply = *call;
        m_pending = false;
        call->deleteLater();

        if (m_dirty) {
            apply();
        } else if (reply.isError() && reply.error().type() != QDBusError::ServiceUnknown
                   && reply.error().name() != QStringLiteral("org.nemomobile.Error.Unknown")) {
            m_retry.start();
        }
    });
}
