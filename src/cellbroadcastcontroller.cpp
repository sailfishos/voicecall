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
const char TopicsProperty[] = "Topics";

QString sanitizeKey(const QString &value)
{
    QString sanitized;
    for (const QChar &ch : value) {
        sanitized.append(ch.isLetterOrNumber() ? ch : QLatin1Char('_'));
    }
    return sanitized.isEmpty() ? QStringLiteral("unknown") : sanitized;
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
    void getPropertiesFinished(const QVariantMap &properties, const QDBusError *error) override
    {
        const QString errorString = error ? error->message() : QString();
        QOfonoCellBroadcast::getPropertiesFinished(properties, error);
        m_controller->cellBroadcastPropertiesFinished(errorString);
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
    QVariantList roamingOptionalChannels;
    CellBroadcastTopicRangeList managedTopics;
};

CellBroadcastController::CellBroadcastController(QObject *parent)
    : QObject(parent)
    , m_emergencyMode(false)
    , m_alertsEnabled(true)
    , m_available(false)
    , m_catalogLoaded(false)
    , m_haveTopics(false)
    , m_applyAfterRefresh(false)
    , m_pendingTopicsOperation(NoTopicsOperation)
    , m_alertsEnabledItem(new MDConfItem(QString::fromLatin1(AlertsEnabledSettingKey), this))
    , m_cellBroadcast(new CellBroadcastOfonoClient(this))
{
    m_alertsEnabled = m_alertsEnabledItem->value(true).toBool();
    connect(m_alertsEnabledItem, &MDConfItem::valueChanged,
            this, &CellBroadcastController::onAlertsEnabledSettingChanged);
    connect(m_cellBroadcast, &QOfonoCellBroadcast::topicsChanged,
            this, &CellBroadcastController::onCellBroadcastTopicsChanged);
    connect(m_cellBroadcast, &QOfonoCellBroadcast::validChanged,
            this, &CellBroadcastController::onCellBroadcastValidChanged);
    connect(m_cellBroadcast, &QOfonoCellBroadcast::incomingBroadcast,
            this, [this](const QString &text, quint16 channel) {
                Q_EMIT incomingBroadcast(text, channel);
            });
    connect(m_cellBroadcast, &QOfonoCellBroadcast::emergencyBroadcast,
            this, [this](const QString &text, const QVariantMap &properties) {
                Q_EMIT emergencyBroadcast(text, properties);
            });
    QTimer::singleShot(0, this, &CellBroadcastController::refresh);
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
    m_cellBroadcast->setModemPath(path);
    recalculate();
    Q_EMIT modemPathChanged();
    refresh();
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

    if (enabled) {
        refreshAndApply();
    } else {
        disableCellBroadcast();
    }
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

QVariantList CellBroadcastController::roamingOptionalChannels() const
{
    return m_roamingOptionalChannels;
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

CellBroadcastAttentionProfile CellBroadcastController::attentionProfileForChannel(int channel)
{
    ensureCatalog();
    if (!m_catalog.isValid() || !m_alertsEnabled) {
        return CellBroadcastAttentionProfile();
    }

    const ActiveCatalogEntry active = activeEntry();
    for (const CellBroadcastCatalogCategory &category : active.entry.categories) {
        if (category.attentionProfile.isEmpty()
                || !channelInRanges(channel, category.ranges)) {
            continue;
        }

        if (!mandatoryChannel(channel, category.ranges)
                && !channelEnabled(active.scope, category)) {
            return CellBroadcastAttentionProfile();
        }

        return m_catalog.attentionProfile(category.attentionProfile);
    }

    return CellBroadcastAttentionProfile();
}

CellBroadcastAttentionProfile CellBroadcastController::configuredAttentionProfile()
{
    ensureCatalog();
    if (!m_catalog.isValid()) {
        return CellBroadcastAttentionProfile();
    }

    const ActiveCatalogEntry active = activeEntry();
    return m_catalog.attentionProfile(active.entry.defaultAttentionProfile);
}

void CellBroadcastController::refresh()
{
    if (m_modemPath.isEmpty()) {
        return;
    }

    m_cellBroadcast->refresh();
}

void CellBroadcastController::refreshAndApply()
{
    if (!m_alertsEnabled) {
        disableCellBroadcast();
        return;
    }
    m_applyAfterRefresh = true;
    refresh();
}

void CellBroadcastController::apply()
{
    ensureCatalog();
    if (!m_alertsEnabled) {
        disableCellBroadcast();
        return;
    }
    if (!m_catalog.isValid() || m_modemPath.isEmpty()) {
        return;
    }
    if (!m_haveTopics) {
        refreshAndApply();
        return;
    }

    const ResolvedState state = resolve();
    const CellBroadcastTopicRangeList previousManaged = lastManagedTopics();
    const CellBroadcastTopicRangeList unmanaged = CellBroadcastTopics::subtract(m_currentTopics,
                                                                                previousManaged);
    const CellBroadcastTopicRangeList topics = CellBroadcastTopics::unite(unmanaged,
                                                                          state.managedTopics);
    if (CellBroadcastTopics::equals(topics, m_currentTopics)
            && CellBroadcastTopics::equals(state.managedTopics, previousManaged)) {
        return;
    }
    setTopics(topics, state.managedTopics);
}

void CellBroadcastController::setChannelEnabled(const QString &categoryId,
                                                const QString &scope,
                                                bool enabled)
{
    if (categoryId.isEmpty()) {
        return;
    }

    settingItem(channelSettingKey(identity(),
                                  scope.isEmpty() ? QStringLiteral("home") : scope,
                                  categoryId))->set(enabled);
    settingItem(channelPatternSettingKey(identity(), categoryId))->set(enabled);
    recalculate();
    if (m_alertsEnabled) {
        apply();
    }
}

void CellBroadcastController::removeUnknownTopic(const QString &topics)
{
    const CellBroadcastTopicRangeList remove = CellBroadcastTopics::parse(topics);
    if (remove.isEmpty() || !m_haveTopics) {
        return;
    }
    setTopics(CellBroadcastTopics::subtract(m_currentTopics, remove),
              lastManagedTopics());
}

void CellBroadcastController::resetToRecommended()
{
    ensureCatalog();
    if (!m_alertsEnabled || !m_catalog.isValid()) {
        return;
    }
    const ResolvedState state = resolve();
    setTopics(state.managedTopics, state.managedTopics);
}

void CellBroadcastController::onAlertsEnabledSettingChanged()
{
    const bool enabled = m_alertsEnabledItem->value(true).toBool();
    setAlertsEnabledValue(enabled);
    if (enabled) {
        refreshAndApply();
    } else {
        disableCellBroadcast();
    }
}

void CellBroadcastController::onCellBroadcastTopicsChanged(const QString &topics)
{
    updateCurrentTopics(topics);
    if (m_alertsEnabled && m_pendingTopicsOperation == NoTopicsOperation) {
        apply();
    }
}

void CellBroadcastController::onCellBroadcastValidChanged(bool valid)
{
    setAvailable(valid);
    if (!valid) {
        m_haveTopics = false;
        recalculate();
        return;
    }

    updateCurrentTopics(m_cellBroadcast->topics());
}

void CellBroadcastController::updateCurrentTopics(const QString &topics)
{
    m_currentTopics = CellBroadcastTopics::parse(topics);
    m_haveTopics = true;
    recalculate();
}

void CellBroadcastController::cellBroadcastPropertiesFinished(const QString &errorString)
{
    if (!errorString.isEmpty()) {
        setAvailable(false);
        setErrorString(errorString);
        m_haveTopics = false;
        recalculate();
        return;
    }

    updateCurrentTopics(m_cellBroadcast->topics());
    setAvailable(true);
    if (m_catalog.isValid()) {
        setErrorString(QString());
    }

    if (m_applyAfterRefresh) {
        m_applyAfterRefresh = false;
        apply();
    }
}

void CellBroadcastController::cellBroadcastPropertySetFinished(const QString &property,
                                                               const QString &errorString)
{
    if (property != QLatin1String(TopicsProperty)) {
        return;
    }

    if (!errorString.isEmpty()) {
        setErrorString(errorString);
        m_pendingTopicsOperation = NoTopicsOperation;
        return;
    }

    const PendingTopicsOperation operation = m_pendingTopicsOperation;
    m_pendingTopicsOperation = NoTopicsOperation;
    if (operation == DisableTopicsOperation) {
        m_currentTopics.clear();
        setErrorString(QString());
        recalculate();
        if (m_alertsEnabled) {
            refreshAndApply();
        }
    } else if (operation == SetTopicsOperation) {
        m_currentTopics = CellBroadcastTopics::unite(CellBroadcastTopicRangeList(), m_currentTopics);
        setLastManagedTopics(m_pendingManagedTopics);
        m_pendingManagedTopics.clear();
        refresh();
    }
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
            && m_roamingOptionalChannels == state.roamingOptionalChannels
            && m_unknownTopics == unknownTopics) {
        return;
    }

    m_mandatoryChannels = state.mandatoryChannels;
    m_optionalChannels = state.optionalChannels;
    m_roamingOptionalChannels = state.roamingOptionalChannels;
    m_unknownTopics = unknownTopics;
    Q_EMIT channelsChanged();
}

CellBroadcastController::ResolvedState CellBroadcastController::resolve() const
{
    ResolvedState state;
    if (!m_catalog.isValid() || !m_alertsEnabled) {
        return state;
    }

    const ActiveCatalogEntry active = activeEntry();

    auto addEntry = [&](const CellBroadcastCatalogEntry &entry,
                        const QString &scope,
                        const QString &plmn,
                        bool roaming) {
        for (const CellBroadcastCatalogCategory &category : entry.categories) {
            const CellBroadcastTopicRangeList mandatory =
                    catalogRangesToTopics(category.ranges, true, true);
            const CellBroadcastTopicRangeList mandatoryDisplay =
                    CellBroadcastTopics::unite(mandatory,
                                               catalogRangesToTopics(category.ranges, true, false));
            if (!mandatoryDisplay.isEmpty()) {
                state.mandatoryChannels.append(channelMap(category.id, category.name,
                                                          category.customName,
                                                          entry.alertSystem, scope, plmn,
                                                          roaming, mandatoryDisplay, true));
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

            const bool enabled = channelEnabled(scope, category);
            QVariantMap map = channelMap(category.id, category.name, category.customName,
                                         entry.alertSystem, scope, plmn,
                                         roaming, optionalDisplay, enabled);
            state.optionalChannels.append(map);
            if (enabled) {
                state.managedTopics = CellBroadcastTopics::unite(state.managedTopics, optional);
            }
        }
    };

    addEntry(active.entry, active.scope, active.plmn, active.roaming);

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
        active.plmn = m_networkMcc + m_networkMnc;
        active.roaming = isRoamingNetworkRelevant();
        return active;
    }

    const CellBroadcastCatalogEntry homeEntry = m_catalog.configuredEntryForPlmn(m_simMcc, m_simMnc);
    if (homeEntry.isValid()) {
        active.entry = homeEntry;
        active.scope = QStringLiteral("home");
        active.plmn = m_simMcc + m_simMnc;
        active.roaming = false;
    } else {
        active.entry = m_catalog.entryForKey(QString());
        active.scope = QStringLiteral("default");
        active.plmn = active.entry.plmn;
        active.roaming = false;
    }

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
    const QString networkPlmn = m_networkMcc + m_networkMnc;
    const QString simPlmn = m_simMcc + m_simMnc;
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

void CellBroadcastController::disableCellBroadcast()
{
    m_applyAfterRefresh = false;
    m_pendingManagedTopics.clear();

    if (m_modemPath.isEmpty()) {
        return;
    }

    m_currentTopics.clear();
    recalculate();
    m_pendingTopicsOperation = DisableTopicsOperation;
    m_cellBroadcast->setTopics(QString());
}

void CellBroadcastController::setCellBroadcastPowered(bool powered)
{
    if (m_modemPath.isEmpty()) {
        return;
    }

    m_cellBroadcast->setEnabled(powered);
}

void CellBroadcastController::setTopics(const CellBroadcastTopicRangeList &topics,
                                        const CellBroadcastTopicRangeList &managedTopics)
{
    if (m_modemPath.isEmpty()) {
        return;
    }

    const QString topicsString = CellBroadcastTopics::format(topics);
    m_currentTopics = topics;
    m_pendingManagedTopics = managedTopics;
    m_pendingTopicsOperation = SetTopicsOperation;

    setCellBroadcastPowered(true);
    m_cellBroadcast->setTopics(topicsString);
    recalculate();
}

QVariantMap CellBroadcastController::channelMap(const QString &id,
                                                const QString &name,
                                                bool customName,
                                                const QString &alertSystem,
                                                const QString &scope,
                                                const QString &plmn,
                                                bool roaming,
                                                const CellBroadcastTopicRangeList &ranges,
                                                bool enabled) const
{
    QVariantMap map;
    map.insert(QStringLiteral("id"), id);
    map.insert(QStringLiteral("name"), name);
    map.insert(QStringLiteral("customName"), customName);
    map.insert(QStringLiteral("alertSystem"), alertSystem);
    map.insert(QStringLiteral("scope"), scope);
    map.insert(QStringLiteral("plmn"), plmn);
    map.insert(QStringLiteral("roaming"), roaming);
    map.insert(QStringLiteral("enabled"), enabled);
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
