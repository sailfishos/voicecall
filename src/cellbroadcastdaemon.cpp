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
#include "cellbroadcastdaemonpolicy_p.h"
#include "cellbroadcastgeofence.h"
#include "cellbroadcaststore.h"

#include <qofonomanager.h>
#include <qofonomodem.h>
#include <qofononetworkregistration.h>
#include <qofonosimmanager.h>

#include <QDBusConnection>
#include <QDateTime>
#include <QDebug>
#include <QFileInfo>
#include <QSet>

namespace {

const char ModemInterface[] = "org.ofono.Modem";
const char SimManagerInterface[] = "org.ofono.SimManager";
const char NetworkRegistrationInterface[] = "org.ofono.NetworkRegistration";
const char AttentionEvent[] = "cellbroadcast_attention";
const char AttentionEventProperty[] = "CellBroadcastAttentionEvent";
const char AttentionHapticSequenceProperty[] = "CellBroadcastAttentionHapticSequence";
const char AttentionReservedUse[] = "official-cell-broadcast-public-warning";
const char AttentionSoundFileProperty[] = "CellBroadcastAttentionSoundFile";
const char AttentionTonePrefix[] = "/usr/share/cell-broadcast-provider-info/attention-tones/";
const char GeoFenceDeadlineProperty[] = "CellBroadcastGeoFenceDeadline";
const char GeoFenceGeometriesProperty[] = "CellBroadcastGeoFenceGeometries";
const char TopicProperty[] = "Topic";
const int DefaultMaximumWaitSeconds = 30;

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
                       profile.event.isEmpty()
                           ? QString::fromLatin1(AttentionEvent) : profile.event);
    properties->insert(QString::fromLatin1(AttentionSoundFileProperty), profile.soundFile);
    const QString hapticSequence = profile.hapticSequence();
    if (!hapticSequence.isEmpty()) {
        properties->insert(QString::fromLatin1(AttentionHapticSequenceProperty),
                           hapticSequence);
    }
    return true;
}

QString geoFenceGeometries(const QVariantMap &alert)
{
    const QString prepared = alert.value(
                QString::fromLatin1(GeoFenceGeometriesProperty)).toString();
    return prepared.isEmpty()
            ? alert.value(QStringLiteral("Geometries")).toString() : prepared;
}

qint64 geoFenceDeadline(const QVariantMap &alert)
{
    const qint64 prepared = alert.value(
                QString::fromLatin1(GeoFenceDeadlineProperty)).toLongLong();
    if (prepared > 0) {
        return prepared;
    }

    int maximumWait = alert.value(QStringLiteral("MaximumWaitTime"),
                                  DefaultMaximumWaitSeconds).toInt();
    if (maximumWait <= 0 || maximumWait == 255) {
        maximumWait = DefaultMaximumWaitSeconds;
    }
    qint64 receivedAt = alert.value(QStringLiteral("ReceivedAt")).toLongLong();
    if (receivedAt <= 0) {
        receivedAt = QDateTime::currentDateTimeUtc().toMSecsSinceEpoch();
    }
    return receivedAt + qint64(qBound(1, maximumWait, 255)) * 1000;
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

CellBroadcastDaemon::CellBroadcastDaemon(QObject *parent, const QString &databasePath)
    : QObject(parent)
    , m_ofonoManager(new QOfonoManager(this))
    , m_geoFence(new CellBroadcastGeoFence(this))
    , m_store(new CellBroadcastStore(databasePath, this))
{
    connect(m_geoFence, &CellBroadcastGeoFence::resolved,
            this, &CellBroadcastDaemon::geoFenceResolved);
    connect(m_ofonoManager, &QOfonoManager::modemsChanged,
            this, [this](const QStringList &) { reloadModems(); });
    connect(m_ofonoManager, &QOfonoManager::availableChanged,
            this, [this](bool) { reloadModems(); });
    connect(m_ofonoManager, &QOfonoManager::modemRemoved,
            this, &CellBroadcastDaemon::modemRemoved);

    QTimer::singleShot(0, this, [this]() {
        const QVariantList alerts = m_store->pendingGeoFenceAlerts();
        for (const QVariant &value : alerts) {
            const QVariantMap alert = value.toMap();
            m_geoFence->check(alert.value(QStringLiteral("RecordId")).toULongLong(),
                              geoFenceGeometries(alert), geoFenceDeadline(alert),
                              alert.value(QStringLiteral("CreatedAt")).toLongLong());
        }
    });
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
        QVariantMap state = controller->stateMap();
        state.insert(QStringLiteral("storageAvailable"), m_store->isDurable());
        state.insert(QStringLiteral("storageError"), m_store->errorString());
        return state;
    }

    QVariantMap state;
    state.insert(QStringLiteral("modemPath"), path);
    state.insert(QStringLiteral("alertsEnabled"), alertsEnabled());
    state.insert(QStringLiteral("available"), false);
    state.insert(QStringLiteral("storageAvailable"), m_store->isDurable());
    state.insert(QStringLiteral("storageError"), m_store->errorString());
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
        controller->refresh();
    }
}

QVariantMap CellBroadcastDaemon::activeAlert() const
{
    return m_store->activeAlert();
}

QVariantMap CellBroadcastDaemon::alert(qulonglong id) const
{
    return m_store->alert(id);
}

QVariantList CellBroadcastDaemon::alertHistory(qulonglong beforeId, int limit) const
{
    return m_store->alertHistory(beforeId, limit);
}

bool CellBroadcastDaemon::acknowledgeAlert(qulonglong id)
{
    if (!m_store->acknowledge(id)) {
        return false;
    }
    const QVariantMap active = m_store->activeAlert();
    if (!active.isEmpty()
            && !active.value(QStringLiteral("SilencedAt")).toLongLong()) {
        Q_EMIT alertAttentionRequested(
                    active.value(QStringLiteral("RecordId")).toULongLong(), active);
    }
    emitActiveAlert(active);
    return true;
}

bool CellBroadcastDaemon::silenceAlert(qulonglong id)
{
    if (!m_store->silence(id)) {
        return false;
    }
    Q_EMIT activeAlertChanged(m_store->activeAlert());
    return true;
}

bool CellBroadcastDaemon::deleteAlert(qulonglong id)
{
    return m_store->remove(id);
}

int CellBroadcastDaemon::clearAlertHistory()
{
    return m_store->clearHistory();
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
    const QString signature = QString::number(channel) + QLatin1Char('|') + text;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const QPair<QString, qint64> detailed = m_lastDetailedBroadcast.value(path);
    if (detailed.first == signature) {
        const qint64 elapsed = now - detailed.second;
        if (elapsed >= 0 && elapsed < 1000) {
            m_lastDetailedBroadcast.remove(path);
            return;
        }
    }
    if (detailed.second && now - detailed.second >= 1000) {
        m_lastDetailedBroadcast.remove(path);
    }

    QVariantMap properties;
    properties.insert(QString::fromLatin1(TopicProperty), channel);
    receiveBroadcast(path, text, properties);
}

void CellBroadcastDaemon::incomingBroadcastWithProperties(
        const QString &path, const QString &text, const QVariantMap &properties)
{
    QVariantMap detailed(properties);
    const int channel = detailed.value(QStringLiteral("MessageIdentifier"),
                                       detailed.value(QString::fromLatin1(TopicProperty))).toInt();
    detailed.insert(QString::fromLatin1(TopicProperty), channel);
    receiveBroadcast(path, text, detailed);
    m_lastDetailedBroadcast.insert(
                path, qMakePair(QString::number(channel) + QLatin1Char('|') + text,
                                QDateTime::currentMSecsSinceEpoch()));
}

void CellBroadcastDaemon::emergencyBroadcast(const QString &path,
                                            const QString &text,
                                            const QVariantMap &properties)
{
    receiveBroadcast(path, text, properties);
}

void CellBroadcastDaemon::receiveBroadcast(const QString &path,
                                           const QString &text,
                                           const QVariantMap &properties)
{
    if (!m_store->isDurable()) {
        qCritical() << "Durable Cell Broadcast storage is unavailable; withholding alert:"
                    << m_store->errorString();
        return;
    }

    QVariantMap alertProperties(properties);
    alertProperties.insert(QStringLiteral("ModemPath"), path);
    if (!alertProperties.contains(QStringLiteral("ReceivedAt"))) {
        alertProperties.insert(QStringLiteral("ReceivedAt"),
                               QDateTime::currentDateTimeUtc().toMSecsSinceEpoch());
    }

    CellBroadcastController *controller = m_controllers.value(path);
    const int channel = alertProperties.value(QStringLiteral("MessageIdentifier"),
                                              alertProperties.value(QString::fromLatin1(
                                                  TopicProperty), -1)).toInt();
    bool attentionAdded = false;
    if (controller && channel >= 0) {
        const QString mcc = alertProperties.value(
                    QStringLiteral("MobileCountryCode")).toString();
        const QString mnc = alertProperties.value(
                    QStringLiteral("MobileNetworkCode")).toString();
        const QVariantMap classified = controller->messagePropertiesForChannel(
                    channel, mcc, mnc);
        for (auto it = classified.begin(); it != classified.end(); ++it) {
            alertProperties.insert(it.key(), it.value());
        }
        attentionAdded = addAttentionProperties(
                    &alertProperties,
                    controller->attentionProfileForChannel(channel, mcc, mnc));
    }
    if (controller && cellBroadcastNeedsEmergencyAttention(properties,
                                                            attentionAdded)) {
        alertProperties.insert(QStringLiteral("CellBroadcastAlertLevel"),
                               QStringLiteral("critical"));
        alertProperties.insert(QStringLiteral("CellBroadcastAttentionPolicy"),
                               QStringLiteral("silent-dnd-override"));
        addAttentionProperties(&alertProperties, controller->emergencyAttentionProfile());
    }
    if (alertProperties.value(QStringLiteral("Primary")).toBool()
            && alertProperties.value(QStringLiteral("EmergencyType")).toString()
               == QLatin1String("Test")) {
        alertProperties.insert(QStringLiteral("CellBroadcastDisplay"),
                               QStringLiteral("none"));
        alertProperties.insert(QStringLiteral("CellBroadcastEnabled"), false);
    }

    const CellBroadcastStore::StoreResult result = m_store->store(text, alertProperties);
    if (!result.stored) {
        qCritical() << "Unable to persist Cell Broadcast alert; withholding presentation:"
                    << m_store->errorString();
        return;
    }

    Q_EMIT alertStored(result.alertId);
    if (result.needsGeoCheck) {
        const QVariantMap alert = m_store->alert(result.alertId);
        m_geoFence->check(result.alertId, geoFenceGeometries(alert),
                          geoFenceDeadline(alert),
                          alert.value(QStringLiteral("CreatedAt")).toLongLong());
    }

    const QString referenceList = alertProperties.value(
                QStringLiteral("DeviceBasedGeoFencingReferenceList")).toString();
    if (!referenceList.isEmpty()) {
        const QVariantList alerts = m_store->prepareGeoFenceTrigger(
                    referenceList,
                    alertProperties.value(
                        QStringLiteral("DeviceBasedGeoFencingType")).toInt(),
                    alertProperties.value(QStringLiteral("ReceivedAt")).toLongLong(),
                    alertProperties);
        for (const QVariant &value : alerts) {
            const QVariantMap alert = value.toMap();
            m_geoFence->check(alert.value(QStringLiteral("RecordId")).toULongLong(),
                              geoFenceGeometries(alert), geoFenceDeadline(alert),
                              alert.value(QStringLiteral("CreatedAt")).toLongLong());
        }
    }
    if (result.requestAttention) {
        QVariantMap attention(alertProperties);
        attention.insert(QStringLiteral("RecordId"), result.alertId);
        Q_EMIT alertAttentionRequested(result.alertId, attention);
    }
    if (result.activeChanged) {
        emitActiveAlert(result.activeAlert);
    } else if (result.presentationChanged) {
        emitLegacyAlert(m_store->alert(result.alertId));
    }
}

void CellBroadcastDaemon::geoFenceResolved(qulonglong id, bool display,
                                           const QString &state)
{
    const CellBroadcastStore::StoreResult result = m_store->resolveGeoFence(id, display, state);
    if (!result.stored) {
        return;
    }
    if (result.requestAttention) {
        const QVariantMap attention = result.activeAlert;
        const quint64 attentionId = attention.value(
                    QStringLiteral("RecordId")).toULongLong();
        Q_EMIT alertAttentionRequested(attentionId, attention);
    }
    if (result.activeChanged) {
        emitActiveAlert(result.activeAlert);
    } else if (result.presentationChanged) {
        emitLegacyAlert(m_store->alert(result.alertId));
    }
}

void CellBroadcastDaemon::emitLegacyAlert(const QVariantMap &alert)
{
    if (!alert.isEmpty()) {
        Q_EMIT broadcastReceived(alert.value(QStringLiteral("ModemPath")).toString(),
                                 alert.value(QStringLiteral("Text")).toString(), alert);
    }
}

void CellBroadcastDaemon::emitActiveAlert(const QVariantMap &alert)
{
    Q_EMIT activeAlertChanged(alert);
    emitLegacyAlert(alert);
}

void CellBroadcastDaemon::applyModem()
{
    QTimer *timer = qobject_cast<QTimer *>(sender());
    if (!timer) {
        return;
    }
    const QString path = timer->property("path").toString();
    if (CellBroadcastController *controller = m_controllers.value(path)) {
        controller->refresh();
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
        connect(controller, &CellBroadcastController::incomingBroadcastWithProperties,
                this, [this, path](const QString &text, const QVariantMap &properties) {
                    incomingBroadcastWithProperties(path, text, properties);
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
    m_lastDetailedBroadcast.remove(path);
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
