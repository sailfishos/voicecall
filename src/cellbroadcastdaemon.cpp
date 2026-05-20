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
#include "cellbroadcastdaemon.h"

#include <qofonomanager.h>
#include <qofonomodem.h>
#include <qofononetworkregistration.h>
#include <qofonosimmanager.h>

#include <QDBusConnection>
#include <QDebug>
#include <QFileInfo>
#include <QSet>

namespace {

const char ModemInterface[] = "org.ofono.Modem";
const char SimManagerInterface[] = "org.ofono.SimManager";
const char NetworkRegistrationInterface[] = "org.ofono.NetworkRegistration";
const char AttentionEvent[] = "cellbroadcast_attention";
const char AttentionEventProperty[] = "CellBroadcastAttentionEvent";
const char AttentionReservedUse[] = "official-cell-broadcast-public-warning";
const char AttentionSoundFileProperty[] = "CellBroadcastAttentionSoundFile";
const char AttentionTonePrefix[] = "/usr/share/cell-broadcast-provider-info/attention-tones/";
const char TopicProperty[] = "Topic";

bool attentionSoundFileAllowed(const QString &path)
{
    return path.startsWith(QLatin1String(AttentionTonePrefix))
            && QFileInfo(path).isFile();
}

bool addAttentionProperties(QVariantMap *properties,
                            const CellBroadcastAttentionProfile &profile)
{
    if (!profile.isValid()) {
        return false;
    }
    if (profile.reservedUse != QLatin1String(AttentionReservedUse)) {
        qWarning() << "Rejected Cell Broadcast attention profile with invalid reserved use"
                   << profile.id;
        return false;
    }
    if (!attentionSoundFileAllowed(profile.soundFile)) {
        qWarning() << "Rejected Cell Broadcast attention sound outside private asset path"
                   << profile.soundFile;
        return false;
    }

    properties->insert(QString::fromLatin1(AttentionEventProperty),
                       QString::fromLatin1(AttentionEvent));
    properties->insert(QString::fromLatin1(AttentionSoundFileProperty), profile.soundFile);
    return true;
}

} // namespace

class ModemSignalWatcher : public QObject
{
    Q_OBJECT

public:
    ModemSignalWatcher(const QString &path, QObject *parent)
        : QObject(parent)
        , m_path(path)
        , m_modem(new QOfonoModem(this))
        , m_simManager(new QOfonoSimManager(this))
        , m_networkRegistration(new QOfonoNetworkRegistration(this))
    {
        connect(m_modem, &QOfonoModem::emergencyChanged,
                this, [this](bool emergency) {
                    Q_EMIT propertyChanged(m_path, QLatin1String(ModemInterface),
                                           QStringLiteral("Emergency"), emergency);
                });
        connect(m_simManager, &QOfonoSimManager::subscriberIdentityChanged,
                this, [this](const QString &imsi) {
                    Q_EMIT propertyChanged(m_path, QLatin1String(SimManagerInterface),
                                           QStringLiteral("SubscriberIdentity"), imsi);
                });
        connect(m_simManager, &QOfonoSimManager::mobileCountryCodeChanged,
                this, [this](const QString &mcc) {
                    Q_EMIT propertyChanged(m_path, QLatin1String(SimManagerInterface),
                                           QStringLiteral("MobileCountryCode"), mcc);
                });
        connect(m_simManager, &QOfonoSimManager::mobileNetworkCodeChanged,
                this, [this](const QString &mnc) {
                    Q_EMIT propertyChanged(m_path, QLatin1String(SimManagerInterface),
                                           QStringLiteral("MobileNetworkCode"), mnc);
                });
        connect(m_networkRegistration, &QOfonoNetworkRegistration::statusChanged,
                this, [this](const QString &status) {
                    Q_EMIT propertyChanged(m_path, QLatin1String(NetworkRegistrationInterface),
                                           QStringLiteral("Status"), status);
                });
        connect(m_networkRegistration, &QOfonoNetworkRegistration::mccChanged,
                this, [this](const QString &mcc) {
                    Q_EMIT propertyChanged(m_path, QLatin1String(NetworkRegistrationInterface),
                                           QStringLiteral("MobileCountryCode"), mcc);
                });
        connect(m_networkRegistration, &QOfonoNetworkRegistration::mncChanged,
                this, [this](const QString &mnc) {
                    Q_EMIT propertyChanged(m_path, QLatin1String(NetworkRegistrationInterface),
                                           QStringLiteral("MobileNetworkCode"), mnc);
                });

        m_modem->setModemPath(path);
        m_simManager->setModemPath(path);
        m_networkRegistration->setModemPath(path);
    }

Q_SIGNALS:
    void propertyChanged(const QString &path,
                         const QString &interface,
                         const QString &property,
                         const QVariant &value);

private:
    QString m_path;
    QOfonoModem *m_modem;
    QOfonoSimManager *m_simManager;
    QOfonoNetworkRegistration *m_networkRegistration;
};

CellBroadcastDaemon::CellBroadcastDaemon(QObject *parent)
    : QObject(parent)
    , m_ofonoManager(new QOfonoManager(this))
{
    connect(m_ofonoManager, &QOfonoManager::modemsChanged,
            this, [this](const QStringList &) { reloadModems(); });
    connect(m_ofonoManager, &QOfonoManager::availableChanged,
            this, [this](bool) { reloadModems(); });
    connect(m_ofonoManager, &QOfonoManager::modemRemoved,
            this, &CellBroadcastDaemon::modemRemoved);

    QTimer::singleShot(0, this, &CellBroadcastDaemon::reloadModems);
}

bool CellBroadcastDaemon::registerObject()
{
    return QDBusConnection::sessionBus().registerObject(
                QStringLiteral("/cellbroadcast"),
                this,
                QDBusConnection::ExportScriptableSlots
                | QDBusConnection::ExportAllSignals);
}

bool CellBroadcastDaemon::alertsEnabled() const
{
    return CellBroadcastController::alertsGloballyEnabled();
}

void CellBroadcastDaemon::setAlertsEnabled(bool enabled)
{
    CellBroadcastController::setAlertsGloballyEnabled(enabled);
    for (CellBroadcastController *controller : m_controllers.values()) {
        controller->setAlertsEnabled(enabled);
    }
    Q_EMIT alertsEnabledChanged(enabled);
}

QVariantMap CellBroadcastDaemon::modemState(const QString &path) const
{
    if (CellBroadcastController *controller = m_controllers.value(path)) {
        return controller->stateMap();
    }

    QVariantMap state;
    state.insert(QStringLiteral("modemPath"), path);
    state.insert(QStringLiteral("alertsEnabled"), alertsEnabled());
    state.insert(QStringLiteral("available"), false);
    return state;
}

void CellBroadcastDaemon::setChannelEnabled(const QString &path,
                                            const QString &categoryId,
                                            const QString &scope,
                                            bool enabled)
{
    if (CellBroadcastController *controller = m_controllers.value(path)) {
        controller->setChannelEnabled(categoryId, scope, enabled);
    }
}

void CellBroadcastDaemon::removeUnknownTopic(const QString &path, const QString &topics)
{
    if (CellBroadcastController *controller = m_controllers.value(path)) {
        controller->removeUnknownTopic(topics);
    }
}

void CellBroadcastDaemon::resetToRecommended(const QString &path)
{
    if (CellBroadcastController *controller = m_controllers.value(path)) {
        controller->resetToRecommended();
    }
}

void CellBroadcastDaemon::refresh(const QString &path)
{
    if (path.isEmpty()) {
        reloadModems();
    } else if (CellBroadcastController *controller = m_controllers.value(path)) {
        controller->refreshAndApply();
    }
}

bool CellBroadcastDaemon::triggerTestBroadcast(const QString &path,
                                               const QString &text,
                                               int channel,
                                               bool emergencyAlert)
{
    QString modemPath(path);
    if (modemPath.isEmpty()) {
        for (const QString &candidate : m_ofonoManager->modems()) {
            if (m_controllers.contains(candidate)) {
                modemPath = candidate;
                break;
            }
        }
    }

    if (modemPath.isEmpty() || !m_controllers.contains(modemPath)) {
        qWarning() << "Unable to trigger Cell Broadcast test alert for unknown modem"
                   << path;
        return false;
    }

    if (emergencyAlert) {
        QVariantMap properties;
        properties.insert(QString::fromLatin1(TopicProperty), channel);
        properties.insert(QStringLiteral("EmergencyAlert"), true);
        emergencyBroadcast(modemPath, text, properties);
    } else {
        incomingBroadcast(modemPath, text, channel);
    }
    return true;
}

void CellBroadcastDaemon::reloadModems()
{
    QSet<QString> seen;
    for (const QString &path : m_ofonoManager->modems()) {
        seen.insert(path);
        ensureModem(path);
    }

    const QStringList known = m_controllers.keys();
    for (const QString &path : known) {
        if (!seen.contains(path)) {
            removeModem(path);
        }
    }

    Q_EMIT modemsChanged();
}

void CellBroadcastDaemon::modemRemoved(const QString &path)
{
    removeModem(path);
}

void CellBroadcastDaemon::propertyChanged(const QString &path,
                                          const QString &interface,
                                          const QString &property,
                                          const QVariant &value)
{
    QVariantMap properties;
    properties.insert(property, value);
    updateProperties(path, interface, properties);
    scheduleApply(path);
}

void CellBroadcastDaemon::incomingBroadcast(const QString &path, const QString &text, int channel)
{
    QVariantMap properties;
    properties.insert(QString::fromLatin1(TopicProperty), channel);

    CellBroadcastController *controller = m_controllers.value(path);
    if (controller) {
        addAttentionProperties(&properties, controller->attentionProfileForChannel(channel));
    }

    Q_EMIT broadcastReceived(path, text, properties);
}

void CellBroadcastDaemon::emergencyBroadcast(const QString &path,
                                            const QString &text,
                                            const QVariantMap &properties)
{
    QVariantMap alertProperties(properties);

    CellBroadcastController *controller = m_controllers.value(path);
    if (controller && properties.value(QStringLiteral("EmergencyAlert")).toBool()) {
        addAttentionProperties(&alertProperties, controller->configuredAttentionProfile());
    }

    Q_EMIT broadcastReceived(path, text, alertProperties);
}

void CellBroadcastDaemon::applyModem()
{
    QTimer *timer = qobject_cast<QTimer *>(sender());
    if (!timer) {
        return;
    }
    const QString path = timer->property("path").toString();
    if (CellBroadcastController *controller = m_controllers.value(path)) {
        controller->refreshAndApply();
    }
}

void CellBroadcastDaemon::ensureModem(const QString &path)
{
    if (!m_controllers.contains(path)) {
        CellBroadcastController *controller = new CellBroadcastController(this);
        controller->setModemPath(path);
        m_controllers.insert(path, controller);
        connect(controller, &CellBroadcastController::alertsEnabledChanged,
                this, [this, controller]() {
                    Q_EMIT alertsEnabledChanged(controller->alertsEnabled());
                });
        connect(controller, &CellBroadcastController::channelsChanged,
                this, [this, path]() { Q_EMIT modemStateChanged(path); });
        connect(controller, &CellBroadcastController::availableChanged,
                this, [this, path]() { Q_EMIT modemStateChanged(path); });
        connect(controller, &CellBroadcastController::errorStringChanged,
                this, [this, path]() { Q_EMIT modemStateChanged(path); });
        connect(controller, &CellBroadcastController::incomingBroadcast,
                this, [this, path](const QString &text, int channel) {
                    incomingBroadcast(path, text, channel);
                });
        connect(controller, &CellBroadcastController::emergencyBroadcast,
                this, [this, path](const QString &text, const QVariantMap &properties) {
                    emergencyBroadcast(path, text, properties);
                });

        ModemSignalWatcher *watcher = new ModemSignalWatcher(path, this);
        connect(watcher, &ModemSignalWatcher::propertyChanged,
                this, &CellBroadcastDaemon::propertyChanged);
        m_watchers.insert(path, watcher);

        QTimer *timer = new QTimer(this);
        timer->setSingleShot(true);
        timer->setInterval(500);
        timer->setProperty("path", path);
        connect(timer, &QTimer::timeout, this, &CellBroadcastDaemon::applyModem);
        m_applyTimers.insert(path, timer);
        Q_EMIT modemsChanged();
    }

    scheduleApply(path);
}

void CellBroadcastDaemon::removeModem(const QString &path)
{
    delete m_controllers.take(path);
    delete m_watchers.take(path);
    delete m_applyTimers.take(path);
    Q_EMIT modemsChanged();
}

void CellBroadcastDaemon::scheduleApply(const QString &path)
{
    if (QTimer *timer = m_applyTimers.value(path)) {
        timer->start();
    }
}

void CellBroadcastDaemon::updateProperties(const QString &path,
                                           const QString &interface,
                                           const QVariantMap &properties)
{
    CellBroadcastController *controller = m_controllers.value(path);
    if (!controller) {
        return;
    }

    if (interface == QLatin1String(SimManagerInterface)) {
        if (properties.contains(QStringLiteral("SubscriberIdentity"))) {
            controller->setImsi(properties.value(QStringLiteral("SubscriberIdentity")).toString());
            Q_EMIT modemStateChanged(path);
        }
        if (properties.contains(QStringLiteral("MobileCountryCode"))) {
            controller->setSimMcc(properties.value(QStringLiteral("MobileCountryCode")).toString());
            Q_EMIT modemStateChanged(path);
        }
        if (properties.contains(QStringLiteral("MobileNetworkCode"))) {
            controller->setSimMnc(properties.value(QStringLiteral("MobileNetworkCode")).toString());
            Q_EMIT modemStateChanged(path);
        }
    } else if (interface == QLatin1String(NetworkRegistrationInterface)) {
        if (properties.contains(QStringLiteral("Status"))) {
            controller->setNetworkStatus(properties.value(QStringLiteral("Status")).toString());
            Q_EMIT modemStateChanged(path);
        }
        if (properties.contains(QStringLiteral("MobileCountryCode"))) {
            controller->setNetworkMcc(properties.value(QStringLiteral("MobileCountryCode")).toString());
            Q_EMIT modemStateChanged(path);
        }
        if (properties.contains(QStringLiteral("MobileNetworkCode"))) {
            controller->setNetworkMnc(properties.value(QStringLiteral("MobileNetworkCode")).toString());
            Q_EMIT modemStateChanged(path);
        }
    } else if (interface == QLatin1String(ModemInterface)) {
        if (properties.contains(QStringLiteral("Emergency"))) {
            controller->setEmergencyMode(properties.value(QStringLiteral("Emergency")).toBool());
            Q_EMIT modemStateChanged(path);
        }
    }
}

#include "cellbroadcastdaemon.moc"
