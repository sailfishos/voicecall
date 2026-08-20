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
#include "cellbroadcastcontroller.h"

#include <MDConfItem>
#include <qofonocellbroadcast.h>

#include <QDBusError>
#include <QTimer>
#include <QVariantMap>

namespace {

const char AlertsEnabledSettingKey[] = "/apps/sailfish/cellbroadcast/enabled";
const char PoweredProperty[] = "Powered";
const char TopicsProperty[] = "Topics";
const int PropertyRetryInterval = 5000;

QString sanitizeKey(const QString &value)
{
    QString sanitized;
    for (const QChar &ch : value) {
        sanitized.append(ch.isLetterOrNumber() ? ch : QLatin1Char('_'));
    }
    return sanitized.isEmpty() ? QStringLiteral("unknown") : sanitized;
}

QString normalizedPlmn(const QString &mcc, const QString &mnc)
{
    return mcc + (mnc.length() == 1
                  ? mnc.rightJustified(2, QLatin1Char('0')) : mnc);
}

bool propertyQueryWillRetry(const QDBusError *error)
{
    if (!error) {
        return false;
    }

    switch (error->type()) {
    case QDBusError::NoReply:
    case QDBusError::Timeout:
    case QDBusError::TimedOut:
        return true;
    default:
        return false;
    }
}

QString settingBaseKey(const QString &identity)
{
    return QStringLiteral("/apps/sailfish/cellbroadcast/") + sanitizeKey(identity);
}

QString channelSettingKey(const QString &identity, const QString &scope, const QString &categoryId)
{
    return settingBaseKey(identity) + QStringLiteral("/channels/")
            + sanitizeKey(scope.isEmpty() ? QStringLiteral("home") : scope)
            + QLatin1Char('/') + sanitizeKey(categoryId);
}

QString channelPatternSettingKey(const QString &identity, const QString &categoryId)
{
    return settingBaseKey(identity) + QStringLiteral("/channel_pattern/")
            + sanitizeKey(categoryId);
}

QString lastManagedSettingKey(const QString &identity, const QString &modemPath)
{
    return settingBaseKey(identity) + QStringLiteral("/modems/")
            + sanitizeKey(modemPath) + QStringLiteral("/last_managed_topics");
}

bool alertsEnabledFromSettings()
{
    MDConfItem item(QString::fromLatin1(AlertsEnabledSettingKey));
    return item.value(true).toBool();
}

CellBroadcastTopicRangeList catalogRangesToTopics(const QList<CellBroadcastCatalogRange> &ranges,
                                                  bool mandatory,
                                                  bool apply)
{
    CellBroadcastTopicRangeList topics;
    for (const CellBroadcastCatalogRange &range : ranges) {
        if (range.mandatory == mandatory && range.apply == apply) {
            topics.append(CellBroadcastTopicRange(range.from, range.to));
        }
    }
    return CellBroadcastTopics::normalize(topics);
}

bool channelInRange(int channel, const CellBroadcastCatalogRange &range)
{
    return channel >= range.from && channel <= range.to;
}

bool channelInRanges(int channel, const QList<CellBroadcastCatalogRange> &ranges)
{
    for (const CellBroadcastCatalogRange &range : ranges) {
        if (channelInRange(channel, range)) {
            return true;
        }
    }
    return false;
}

bool mandatoryChannel(int channel, const QList<CellBroadcastCatalogRange> &ranges)
{
    for (const CellBroadcastCatalogRange &range : ranges) {
        if (range.mandatory && channelInRange(channel, range)) {
            return true;
        }
    }
    return false;
}

} // namespace

class CellBroadcastOfonoClient : public QOfonoCellBroadcast
{
public:
    explicit CellBroadcastOfonoClient(CellBroadcastController *controller)
        : QOfonoCellBroadcast(controller)
        , m_controller(controller)
    {
    }

    void refresh()
    {
        queryProperties();
    }

protected:
    void dbusInterfaceDropped() override
    {
        QOfonoCellBroadcast::dbusInterfaceDropped();
        m_controller->cellBroadcastInterfaceDropped();
    }

    void getPropertiesFinished(const QVariantMap &properties, const QDBusError *error) override
    {
        const bool retrying = propertyQueryWillRetry(error);
        const QString errorString = error ? error->message() : QString();
        QOfonoCellBroadcast::getPropertiesFinished(properties, error);
        if (!retrying) {
            m_controller->cellBroadcastPropertiesFinished(errorString);
        }
    }

    void setPropertyFinished(const QString &property, const QDBusError *error) override
    {
        const QString errorString = error ? error->message() : QString();
        QOfonoObject::setPropertyFinished(property, error);
        m_controller->cellBroadcastPropertySetFinished(property, errorString);
    }

private:
    CellBroadcastController *m_controller;
};

struct CellBroadcastController::ActiveCatalogEntry
{
    CellBroadcastCatalogEntry entry;
    QString scope;
    QString plmn;
    bool roaming;
};

struct CellBroadcastController::ResolvedState
{
    QVariantList mandatoryChannels;
    QVariantList optionalChannels;
    CellBroadcastTopicRangeList managedTopics;
};

CellBroadcastController::CellBroadcastController(QObject *parent)
    : QObject(parent)
    , m_emergencyMode(false)
    , m_alertsEnabled(true)
    , m_available(false)
    , m_catalogLoaded(false)
    , m_haveTopics(false)
    , m_refreshPending(false)
    , m_resetRequested(false)
    , m_pendingPropertyOperation(NoPropertyOperation)
    , m_alertsEnabledItem(new MDConfItem(QString::fromLatin1(AlertsEnabledSettingKey), this))
    , m_cellBroadcast(new CellBroadcastOfonoClient(this))
    , m_retryTimer(new QTimer(this))
{
    m_alertsEnabled = m_alertsEnabledItem->value(true).toBool();
    connect(m_alertsEnabledItem, &MDConfItem::valueChanged,
            this, &CellBroadcastController::onAlertsEnabledSettingChanged);
    connect(m_cellBroadcast, &QOfonoCellBroadcast::topicsChanged,
            this, &CellBroadcastController::onCellBroadcastTopicsChanged);
    connect(m_cellBroadcast, &QOfonoCellBroadcast::enabledChanged,
            this, &CellBroadcastController::onCellBroadcastEnabledChanged);
    connect(m_cellBroadcast, &QOfonoCellBroadcast::validChanged,
            this, &CellBroadcastController::onCellBroadcastValidChanged);
    connect(m_cellBroadcast, &QOfonoCellBroadcast::incomingBroadcast,
            this, [this](const QString &text, quint16 channel) {
                Q_EMIT incomingBroadcast(text, channel);
            });
    connect(m_cellBroadcast, &QOfonoCellBroadcast::incomingBroadcastWithProperties,
            this, [this](const QString &text, const QVariantMap &properties) {
                Q_EMIT incomingBroadcastWithProperties(text, properties);
            });
    connect(m_cellBroadcast, &QOfonoCellBroadcast::emergencyBroadcast,
            this, [this](const QString &text, const QVariantMap &properties) {
                Q_EMIT emergencyBroadcast(text, properties);
            });
    m_retryTimer->setSingleShot(true);
    m_retryTimer->setInterval(PropertyRetryInterval);
    connect(m_retryTimer, &QTimer::timeout,
            this, &CellBroadcastController::refresh);
}

bool CellBroadcastController::alertsGloballyEnabled()
{
    return alertsEnabledFromSettings();
}

void CellBroadcastController::setAlertsGloballyEnabled(bool enabled)
{
    MDConfItem item(QString::fromLatin1(AlertsEnabledSettingKey));
    item.set(enabled);
}

QString CellBroadcastController::catalogPath() const
{
    return m_catalogPath;
}

void CellBroadcastController::setCatalogPath(const QString &path)
{
    if (m_catalogPath == path) {
        return;
    }
    m_catalogPath = path;
    m_catalogLoaded = false;
    ensureCatalog();
    recalculate();
    Q_EMIT catalogPathChanged();
}

QString CellBroadcastController::modemPath() const
{
    return m_modemPath;
}

void CellBroadcastController::setModemPath(const QString &path)
{
    if (m_modemPath == path) {
        return;
    }
    m_modemPath = path;
    m_haveTopics = false;
    // QOfono starts GetProperties when a modem interface is attached. Track
    // that implicit request instead of issuing a second query here.
    m_refreshPending = !path.isEmpty();
    m_cellBroadcast->setModemPath(path);
    recalculate();
    Q_EMIT modemPathChanged();
}

QString CellBroadcastController::imsi() const
{
    return m_imsi;
}

void CellBroadcastController::setImsi(const QString &imsi)
{
    if (m_imsi == imsi) {
        return;
    }
    m_imsi = imsi;
    recalculate();
    Q_EMIT imsiChanged();
}

QString CellBroadcastController::simMcc() const
{
    return m_simMcc;
}

void CellBroadcastController::setSimMcc(const QString &mcc)
{
    if (m_simMcc == mcc) {
        return;
    }
    m_simMcc = mcc;
    recalculate();
    Q_EMIT simMccChanged();
}

QString CellBroadcastController::simMnc() const
{
    return m_simMnc;
}

void CellBroadcastController::setSimMnc(const QString &mnc)
{
    if (m_simMnc == mnc) {
        return;
    }
    m_simMnc = mnc;
    recalculate();
    Q_EMIT simMncChanged();
}

QString CellBroadcastController::networkMcc() const
{
    return m_networkMcc;
}

void CellBroadcastController::setNetworkMcc(const QString &mcc)
{
    if (m_networkMcc == mcc) {
        return;
    }
    m_networkMcc = mcc;
    recalculate();
    Q_EMIT networkMccChanged();
}

QString CellBroadcastController::networkMnc() const
{
    return m_networkMnc;
}

void CellBroadcastController::setNetworkMnc(const QString &mnc)
{
    if (m_networkMnc == mnc) {
        return;
    }
    m_networkMnc = mnc;
    recalculate();
    Q_EMIT networkMncChanged();
}

QString CellBroadcastController::networkStatus() const
{
    return m_networkStatus;
}

void CellBroadcastController::setNetworkStatus(const QString &status)
{
    if (m_networkStatus == status) {
        return;
    }
    m_networkStatus = status;
    recalculate();
    Q_EMIT networkStatusChanged();
}

bool CellBroadcastController::emergencyMode() const
{
    return m_emergencyMode;
}

void CellBroadcastController::setEmergencyMode(bool emergencyMode)
{
    if (m_emergencyMode == emergencyMode) {
        return;
    }
    m_emergencyMode = emergencyMode;
    recalculate();
    Q_EMIT emergencyModeChanged();
}

bool CellBroadcastController::alertsEnabled() const
{
    return m_alertsEnabled;
}

void CellBroadcastController::setAlertsEnabled(bool enabled)
{
    m_alertsEnabledItem->set(enabled);
    setAlertsEnabledValue(enabled);
    processTopicUpdate();
}

bool CellBroadcastController::available() const
{
    return m_available;
}

QString CellBroadcastController::errorString() const
{
    return m_errorString;
}

QVariantList CellBroadcastController::mandatoryChannels() const
{
    return m_mandatoryChannels;
}

QVariantList CellBroadcastController::optionalChannels() const
{
    return m_optionalChannels;
}

QVariantList CellBroadcastController::unknownTopics() const
{
    return m_unknownTopics;
}

QVariantMap CellBroadcastController::stateMap() const
{
    QVariantMap state;
    state.insert(QStringLiteral("modemPath"), m_modemPath);
    state.insert(QStringLiteral("imsi"), m_imsi);
    state.insert(QStringLiteral("simMcc"), m_simMcc);
    state.insert(QStringLiteral("simMnc"), m_simMnc);
    state.insert(QStringLiteral("networkMcc"), m_networkMcc);
    state.insert(QStringLiteral("networkMnc"), m_networkMnc);
    state.insert(QStringLiteral("networkStatus"), m_networkStatus);
    state.insert(QStringLiteral("emergencyMode"), m_emergencyMode);
    state.insert(QStringLiteral("alertsEnabled"), m_alertsEnabled);
    state.insert(QStringLiteral("available"), m_available);
    state.insert(QStringLiteral("errorString"), m_errorString);
    state.insert(QStringLiteral("mandatoryChannels"), m_mandatoryChannels);
    state.insert(QStringLiteral("optionalChannels"), m_optionalChannels);
    state.insert(QStringLiteral("unknownTopics"), m_unknownTopics);
    return state;
}

CellBroadcastAttentionProfile CellBroadcastController::attentionProfileForChannel(
        int channel, const QString &mcc, const QString &mnc)
{
    ensureCatalog();
    if (!m_catalog.isValid()) {
        return CellBroadcastAttentionProfile();
    }

    const ActiveCatalogEntry active = activeEntryForPlmn(mcc, mnc);
    for (const CellBroadcastCatalogCategory &category : active.entry.categories) {
        if (category.attentionProfile.isEmpty()
                || !channelInRanges(channel, category.ranges)) {
            continue;
        }

        if (!mandatoryChannel(channel, category.ranges)
                && (!m_alertsEnabled || !channelEnabled(active.scope, category))) {
            return CellBroadcastAttentionProfile();
        }

        return m_catalog.attentionProfile(category.attentionProfile);
    }

    return CellBroadcastAttentionProfile();
}

QVariantMap CellBroadcastController::messagePropertiesForChannel(
        int channel, const QString &mcc, const QString &mnc)
{
    ensureCatalog();
    QVariantMap properties;
    if (!m_catalog.isValid()) {
        return properties;
    }

    const ActiveCatalogEntry active = activeEntryForPlmn(mcc, mnc);
    for (const CellBroadcastCatalogCategory &category : active.entry.categories) {
        for (const CellBroadcastCatalogRange &range : category.ranges) {
            if (!channelInRange(channel, range)) {
                continue;
            }

            properties.insert(QStringLiteral("CellBroadcastCategory"), category.id);
            properties.insert(QStringLiteral("CellBroadcastTitle"), category.title);
            properties.insert(QStringLiteral("CellBroadcastAlertLevel"), category.alertLevel);
            properties.insert(QStringLiteral("CellBroadcastAttentionPolicy"),
                              category.attentionPolicy);
            properties.insert(QStringLiteral("CellBroadcastDisplay"), category.display);
            properties.insert(QStringLiteral("CellBroadcastSourceRef"), category.sourceRef);
            properties.insert(QStringLiteral("CellBroadcastLanguageRole"), range.languageRole);
            properties.insert(QStringLiteral("CellBroadcastUserConfigurable"),
                              category.userConfigurable);
            properties.insert(QStringLiteral("CellBroadcastSettingsVisible"),
                              category.settingsVisible);
            properties.insert(QStringLiteral("CellBroadcastMandatory"), range.mandatory);
            properties.insert(QStringLiteral("CellBroadcastEnabled"),
                              range.mandatory || (m_alertsEnabled
                                  && channelEnabled(active.scope, category)));
            properties.insert(QStringLiteral("CellBroadcastPlmn"), active.plmn);
            return properties;
        }
    }

    return properties;
}

CellBroadcastAttentionProfile CellBroadcastController::emergencyAttentionProfile()
{
    ensureCatalog();
    if (!m_catalog.isValid()) {
        return CellBroadcastAttentionProfile();
    }

    const CellBroadcastAttentionProfile critical = m_catalog.attentionProfile(
                QStringLiteral("critical"));
    if (critical.isValid()) {
        return critical;
    }

    // Preserve compatibility with catalogs predating the generic critical
    // profile. This fallback remains profile-controlled rather than dropping
    // the emergency attention indication entirely.
    const ActiveCatalogEntry active = activeEntry();
    return m_catalog.attentionProfile(active.entry.defaultAttentionProfile);
}

void CellBroadcastController::refresh()
{
    if (m_modemPath.isEmpty() || m_refreshPending
            || m_pendingPropertyOperation != NoPropertyOperation) {
        return;
    }

    m_refreshPending = true;
    m_cellBroadcast->refresh();
}

void CellBroadcastController::setChannelEnabled(const QString &categoryId,
                                                const QString &scope,
                                                bool enabled)
{
    if (categoryId.isEmpty()) {
        return;
    }

    ensureCatalog();
    const ActiveCatalogEntry active = activeEntry();
    for (const CellBroadcastCatalogCategory &category : active.entry.categories) {
        if (category.id == categoryId && !category.userConfigurable) {
            return;
        }
    }

    settingItem(channelSettingKey(identity(),
                                  scope.isEmpty() ? QStringLiteral("home") : scope,
                                  categoryId))->set(enabled);
    settingItem(channelPatternSettingKey(identity(), categoryId))->set(enabled);
    recalculate();
    processTopicUpdate();
}

void CellBroadcastController::removeUnknownTopic(const QString &topics)
{
    const CellBroadcastTopicRangeList remove = CellBroadcastTopics::parse(topics);
    if (remove.isEmpty() || !m_haveTopics) {
        return;
    }

    const ResolvedState state = resolve();
    const CellBroadcastTopicRangeList protectedTopics = CellBroadcastTopics::unite(
                lastManagedTopics(), state.managedTopics);
    const CellBroadcastTopicRangeList unknownTopics = CellBroadcastTopics::subtract(
                m_currentTopics, protectedTopics);
    const CellBroadcastTopicRangeList allowedRemove = CellBroadcastTopics::subtract(
                remove, CellBroadcastTopics::subtract(remove, unknownTopics));
    if (allowedRemove.isEmpty()) {
        return;
    }
    m_requestedTopicRemovals = CellBroadcastTopics::unite(
                m_requestedTopicRemovals, allowedRemove);
    processTopicUpdate();
}

void CellBroadcastController::resetToRecommended()
{
    ensureCatalog();
    if (!m_catalog.isValid()) {
        return;
    }
    m_resetRequested = true;
    processTopicUpdate();
}

void CellBroadcastController::onAlertsEnabledSettingChanged()
{
    const bool enabled = m_alertsEnabledItem->value(true).toBool();
    setAlertsEnabledValue(enabled);
    processTopicUpdate();
}

void CellBroadcastController::onCellBroadcastEnabledChanged(bool)
{
    processTopicUpdate();
}

void CellBroadcastController::onCellBroadcastTopicsChanged(const QString &topics)
{
    updateCurrentTopics(topics);
    processTopicUpdate();
}

void CellBroadcastController::onCellBroadcastValidChanged(bool valid)
{
    setAvailable(valid);
    if (!valid) {
        m_haveTopics = false;
        recalculate();
        return;
    }

    // This may be either a completed implicit reattach query or restored modem
    // validity with cached properties. In both cases the current state is now
    // authoritative enough to resume convergence.
    m_refreshPending = false;
    updateCurrentTopics(m_cellBroadcast->topics());
    processTopicUpdate();
}

void CellBroadcastController::cellBroadcastInterfaceDropped()
{
    // QOfono owns pending calls through its D-Bus interface. Dropping the
    // interface destroys an in-flight SetProperty watcher without a completion
    // callback. Preserve explicit reset/removal intents for the implicit
    // reattach query to apply.
    m_refreshPending = !m_modemPath.isEmpty();
    m_pendingPropertyOperation = NoPropertyOperation;
    m_inFlightManagedTopics.clear();
    m_haveTopics = false;
    recalculate();
}

void CellBroadcastController::updateCurrentTopics(const QString &topics)
{
    m_currentTopics = CellBroadcastTopics::parse(topics);
    m_haveTopics = true;
    recalculate();
}

void CellBroadcastController::cellBroadcastPropertiesFinished(const QString &errorString)
{
    m_refreshPending = false;
    if (!errorString.isEmpty()) {
        setAvailable(false);
        setErrorString(errorString);
        m_haveTopics = false;
        recalculate();
        m_retryTimer->start();
        return;
    }

    m_retryTimer->stop();
    updateCurrentTopics(m_cellBroadcast->topics());
    setAvailable(true);
    if (m_catalog.isValid()) {
        setErrorString(QString());
    }

    processTopicUpdate();
}

void CellBroadcastController::cellBroadcastPropertySetFinished(const QString &property,
                                                               const QString &errorString)
{
    const PendingPropertyOperation operation = m_pendingPropertyOperation;
    const bool expectedProperty = (operation == SetPoweredOperation
                                   && property == QLatin1String(PoweredProperty))
            || (operation == SetTopicsOperation
                && property == QLatin1String(TopicsProperty));
    if (!expectedProperty) {
        return;
    }

    m_pendingPropertyOperation = NoPropertyOperation;
    if (!errorString.isEmpty()) {
        setErrorString(errorString);
        m_haveTopics = false;
        m_inFlightManagedTopics.clear();
        m_retryTimer->start();
        return;
    }

    if (operation == SetTopicsOperation) {
        setLastManagedTopics(m_inFlightManagedTopics);
    }
    m_inFlightManagedTopics.clear();
    refresh();
}

void CellBroadcastController::ensureCatalog()
{
    if (m_catalogLoaded) {
        return;
    }

    m_catalogLoaded = true;
    if (!m_catalog.load(m_catalogPath)) {
        setErrorString(m_catalog.errorString());
    }
}

void CellBroadcastController::recalculate()
{
    ensureCatalog();
    const ResolvedState state = resolve();
    const CellBroadcastTopicRangeList previousManaged = lastManagedTopics();
    const CellBroadcastTopicRangeList knownManaged = CellBroadcastTopics::unite(previousManaged,
                                                                               state.managedTopics);
    const CellBroadcastTopicRangeList unknown = CellBroadcastTopics::subtract(m_currentTopics,
                                                                              knownManaged);

    QVariantList unknownTopics;
    for (const QVariant &value : CellBroadcastTopics::toVariantList(unknown)) {
        unknownTopics.append(value);
    }

    if (m_mandatoryChannels == state.mandatoryChannels
            && m_optionalChannels == state.optionalChannels
            && m_unknownTopics == unknownTopics) {
        return;
    }

    m_mandatoryChannels = state.mandatoryChannels;
    m_optionalChannels = state.optionalChannels;
    m_unknownTopics = unknownTopics;
    Q_EMIT channelsChanged();
}

CellBroadcastController::ResolvedState CellBroadcastController::resolve() const
{
    ResolvedState state;
    if (!m_catalog.isValid()) {
        return state;
    }

    const ActiveCatalogEntry active = activeEntry();

    for (const CellBroadcastCatalogCategory &category : active.entry.categories) {
        const CellBroadcastTopicRangeList mandatory =
                catalogRangesToTopics(category.ranges, true, true);
        const CellBroadcastTopicRangeList mandatoryDisplay =
                CellBroadcastTopics::unite(mandatory,
                                           catalogRangesToTopics(category.ranges, true, false));
        if (category.settingsVisible && !mandatoryDisplay.isEmpty()) {
            state.mandatoryChannels.append(channelMap(category,
                                                      active.entry.alertSystem,
                                                      active.scope, active.plmn,
                                                      active.roaming,
                                                      mandatoryDisplay, true));
        }
        if (!mandatory.isEmpty()) {
            state.managedTopics = CellBroadcastTopics::unite(state.managedTopics, mandatory);
        }

        const CellBroadcastTopicRangeList optional =
                catalogRangesToTopics(category.ranges, false, true);
        const CellBroadcastTopicRangeList optionalDisplay =
                CellBroadcastTopics::unite(optional,
                                           catalogRangesToTopics(category.ranges, false, false));
        if (optionalDisplay.isEmpty()) {
            continue;
        }

        const bool enabled = m_alertsEnabled && channelEnabled(active.scope, category);
        const QVariantMap map = channelMap(category,
                                           active.entry.alertSystem,
                                           active.scope, active.plmn,
                                           active.roaming,
                                           optionalDisplay, enabled);
        if (category.settingsVisible) {
            state.optionalChannels.append(map);
        }
        if (enabled) {
            state.managedTopics = CellBroadcastTopics::unite(state.managedTopics, optional);
        }
    }

    return state;
}

CellBroadcastController::ActiveCatalogEntry CellBroadcastController::activeEntry() const
{
    ActiveCatalogEntry active;

    if (!m_networkMcc.isEmpty()) {
        // Cell Broadcast is local to the serving network, not the SIM home PLMN.
        active.entry = m_catalog.configuredEntryForPlmn(m_networkMcc, m_networkMnc);
        if (!active.entry.isValid()) {
            active.entry = m_catalog.entryForKey(QString());
        }
        active.scope = active.entry.plmn.isEmpty()
                ? QStringLiteral("default")
                : active.entry.plmn;
        active.plmn = normalizedPlmn(m_networkMcc, m_networkMnc);
        active.roaming = isRoamingNetworkRelevant();
        return active;
    }

    const CellBroadcastCatalogEntry homeEntry = m_catalog.configuredEntryForPlmn(m_simMcc, m_simMnc);
    if (homeEntry.isValid()) {
        active.entry = homeEntry;
        active.scope = QStringLiteral("home");
        active.plmn = normalizedPlmn(m_simMcc, m_simMnc);
        active.roaming = false;
    } else {
        active.entry = m_catalog.entryForKey(QString());
        active.scope = QStringLiteral("default");
        active.plmn = active.entry.plmn;
        active.roaming = false;
    }

    return active;
}

CellBroadcastController::ActiveCatalogEntry CellBroadcastController::activeEntryForPlmn(
        const QString &mcc, const QString &mnc) const
{
    if (mcc.isEmpty()) {
        return activeEntry();
    }

    ActiveCatalogEntry active;
    active.entry = m_catalog.configuredEntryForPlmn(mcc, mnc);
    if (!active.entry.isValid()) {
        active.entry = m_catalog.entryForKey(QString());
    }
    active.scope = active.entry.plmn.isEmpty()
            ? QStringLiteral("default") : active.entry.plmn;
    active.plmn = normalizedPlmn(mcc, mnc);
    active.roaming = false;
    return active;
}

QString CellBroadcastController::identity() const
{
    if (!m_imsi.isEmpty()) {
        return m_imsi;
    }
    return QStringLiteral("modem_") + m_modemPath;
}

bool CellBroadcastController::isRoamingNetworkRelevant() const
{
    if (m_networkMcc.isEmpty()) {
        return false;
    }
    const QString networkPlmn = normalizedPlmn(m_networkMcc, m_networkMnc);
    const QString simPlmn = normalizedPlmn(m_simMcc, m_simMnc);
    if (!simPlmn.isEmpty() && networkPlmn == simPlmn) {
        return false;
    }
    return m_networkStatus == QLatin1String("roaming") || m_emergencyMode;
}

bool CellBroadcastController::channelEnabled(const QString &scope,
                                             const CellBroadcastCatalogCategory &category) const
{
    const QString id = identity();
    const QVariant scopedValue = settingItem(channelSettingKey(id, scope, category.id))->value();
    MDConfItem *patternItem = settingItem(channelPatternSettingKey(id, category.id));

    if (scopedValue.isValid()) {
        if (!patternItem->value().isValid()) {
            patternItem->set(scopedValue);
        }
        return scopedValue.toBool();
    }

    const QVariant patternValue = patternItem->value();
    if (patternValue.isValid()) {
        return patternValue.toBool();
    }

    return category.defaultEnabled;
}

MDConfItem *CellBroadcastController::settingItem(const QString &key) const
{
    MDConfItem *item = m_settingItems.value(key);
    if (!item) {
        item = new MDConfItem(key, const_cast<CellBroadcastController *>(this));
        m_settingItems.insert(key, item);
    }
    return item;
}

CellBroadcastTopicRangeList CellBroadcastController::lastManagedTopics() const
{
    return CellBroadcastTopics::parse(settingItem(lastManagedSettingKey(identity(), m_modemPath))
                                      ->value(QString()).toString());
}

void CellBroadcastController::setLastManagedTopics(const CellBroadcastTopicRangeList &topics)
{
    settingItem(lastManagedSettingKey(identity(), m_modemPath))
            ->set(CellBroadcastTopics::format(topics));
}

void CellBroadcastController::setAlertsEnabledValue(bool enabled)
{
    if (m_alertsEnabled == enabled) {
        return;
    }
    m_alertsEnabled = enabled;
    recalculate();
    Q_EMIT alertsEnabledChanged();
}

void CellBroadcastController::processTopicUpdate()
{
    ensureCatalog();
    if (!m_catalog.isValid()
            || m_modemPath.isEmpty()
            || m_refreshPending
            || m_pendingPropertyOperation != NoPropertyOperation
            || m_retryTimer->isActive()) {
        return;
    }

    if (!m_haveTopics) {
        refresh();
        return;
    }

    if (!m_cellBroadcast->enabled()) {
        m_pendingPropertyOperation = SetPoweredOperation;
        m_cellBroadcast->setEnabled(true);
        return;
    }

    const ResolvedState state = resolve();
    const CellBroadcastTopicRangeList previousManaged = lastManagedTopics();
    CellBroadcastTopicRangeList topics;
    if (m_resetRequested) {
        topics = state.managedTopics;
    } else {
        CellBroadcastTopicRangeList unmanaged = CellBroadcastTopics::subtract(
                    m_currentTopics, previousManaged);
        unmanaged = CellBroadcastTopics::subtract(unmanaged,
                                                  m_requestedTopicRemovals);
        topics = CellBroadcastTopics::unite(unmanaged, state.managedTopics);
    }

    if (CellBroadcastTopics::equals(topics, m_currentTopics)
            && CellBroadcastTopics::equals(state.managedTopics, previousManaged)) {
        m_resetRequested = false;
        m_requestedTopicRemovals.clear();
        if (m_catalog.isValid()) {
            setErrorString(QString());
        }
        return;
    }

    // The modem may apply a Topics write before its asynchronous reply. Keep
    // both the previous and requested ownership until the reply confirms the
    // exact new set, so an interface or process loss cannot make our newly
    // added topics look unmanaged on the next convergence.
    setLastManagedTopics(CellBroadcastTopics::unite(previousManaged,
                                                    state.managedTopics));
    m_inFlightManagedTopics = state.managedTopics;
    m_pendingPropertyOperation = SetTopicsOperation;
    m_cellBroadcast->setTopics(CellBroadcastTopics::format(topics));
}

QVariantMap CellBroadcastController::channelMap(
                                                const CellBroadcastCatalogCategory &category,
                                                const QString &alertSystem,
                                                const QString &scope,
                                                const QString &plmn,
                                                bool roaming,
                                                const CellBroadcastTopicRangeList &ranges,
                                                bool enabled) const
{
    QVariantMap map;
    map.insert(QStringLiteral("id"), category.id);
    map.insert(QStringLiteral("name"), category.name);
    map.insert(QStringLiteral("title"), category.title);
    map.insert(QStringLiteral("alertLevel"), category.alertLevel);
    map.insert(QStringLiteral("customName"), category.customName);
    map.insert(QStringLiteral("alertSystem"), alertSystem);
    map.insert(QStringLiteral("scope"), scope);
    map.insert(QStringLiteral("plmn"), plmn);
    map.insert(QStringLiteral("roaming"), roaming);
    map.insert(QStringLiteral("enabled"), enabled);
    map.insert(QStringLiteral("userConfigurable"), category.userConfigurable);
    map.insert(QStringLiteral("settingsVisible"), category.settingsVisible);
    map.insert(QStringLiteral("topics"), CellBroadcastTopics::format(ranges));
    return map;
}

void CellBroadcastController::setErrorString(const QString &errorString)
{
    if (m_errorString == errorString) {
        return;
    }
    m_errorString = errorString;
    Q_EMIT errorStringChanged();
}

void CellBroadcastController::setAvailable(bool available)
{
    if (m_available == available) {
        return;
    }
    m_available = available;
    Q_EMIT availableChanged();
}
