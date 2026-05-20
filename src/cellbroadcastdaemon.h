/*
 * This file is a part of the Voice Call Manager project
 *
 * Copyright (C) 2026 Jolla Mobile Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */
#ifndef CELLBROADCASTDAEMON_H
#define CELLBROADCASTDAEMON_H

#include "cellbroadcastcontroller.h"

#include <QHash>
#include <QObject>
#include <QTimer>
#include <QVariantMap>

class ModemSignalWatcher;
class QOfonoManager;

class CellBroadcastDaemon : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.nemomobile.voicecall.CellBroadcast")

public:
    explicit CellBroadcastDaemon(QObject *parent = 0);

    bool registerObject();

public Q_SLOTS:
    Q_SCRIPTABLE bool alertsEnabled() const;
    Q_SCRIPTABLE void setAlertsEnabled(bool enabled);
    Q_SCRIPTABLE QVariantMap modemState(const QString &path) const;
    Q_SCRIPTABLE void setChannelEnabled(const QString &path,
                                        const QString &categoryId,
                                        const QString &scope,
                                        bool enabled);
    Q_SCRIPTABLE void removeUnknownTopic(const QString &path, const QString &topics);
    Q_SCRIPTABLE void resetToRecommended(const QString &path);
    Q_SCRIPTABLE void refresh(const QString &path);
    Q_SCRIPTABLE bool triggerTestBroadcast(const QString &path,
                                           const QString &text,
                                           int channel,
                                           bool emergencyAlert);

Q_SIGNALS:
    void alertsEnabledChanged(bool enabled);
    void modemStateChanged(const QString &path);
    void modemsChanged();
    void broadcastReceived(const QString &path,
                           const QString &text,
                           const QVariantMap &properties);

private Q_SLOTS:
    void reloadModems();
    void modemRemoved(const QString &path);
    void propertyChanged(const QString &path,
                         const QString &interface,
                         const QString &property,
                         const QVariant &value);
    void incomingBroadcast(const QString &path, const QString &text, int channel);
    void emergencyBroadcast(const QString &path,
                            const QString &text,
                            const QVariantMap &properties);
    void applyModem();

private:
    void ensureModem(const QString &path);
    void removeModem(const QString &path);
    void scheduleApply(const QString &path);
    void updateProperties(const QString &path,
                          const QString &interface,
                          const QVariantMap &properties);

private:
    QHash<QString, CellBroadcastController *> m_controllers;
    QHash<QString, ModemSignalWatcher *> m_watchers;
    QHash<QString, QTimer *> m_applyTimers;
    QOfonoManager *m_ofonoManager;
};

#endif
