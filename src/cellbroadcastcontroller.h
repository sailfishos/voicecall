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
#ifndef CELLBROADCASTCONTROLLER_H
#define CELLBROADCASTCONTROLLER_H

#include "cellbroadcastcatalog.h"

#include <QHash>
#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

class CellBroadcastOfonoClient;
class MDConfItem;
class QTimer;

class CellBroadcastController : public QObject
{
    Q_OBJECT

public:
    explicit CellBroadcastController(QObject *parent = 0);

    static bool alertsGloballyEnabled();
    static void setAlertsGloballyEnabled(bool enabled);

    QString catalogPath() const;
    void setCatalogPath(const QString &path);

    QString modemPath() const;
    void setModemPath(const QString &path);

    QString imsi() const;
    void setImsi(const QString &imsi);

    QString simMcc() const;
    void setSimMcc(const QString &mcc);

    QString simMnc() const;
    void setSimMnc(const QString &mnc);

    QString networkMcc() const;
    void setNetworkMcc(const QString &mcc);

    QString networkMnc() const;
    void setNetworkMnc(const QString &mnc);

    QString networkStatus() const;
    void setNetworkStatus(const QString &status);

    bool emergencyMode() const;
    void setEmergencyMode(bool emergencyMode);

    bool alertsEnabled() const;
    void setAlertsEnabled(bool enabled);

    bool available() const;
    QString errorString() const;

    QVariantList mandatoryChannels() const;
    QVariantList optionalChannels() const;
    QVariantList unknownTopics() const;
    QVariantMap stateMap() const;

    CellBroadcastAttentionProfile attentionProfileForChannel(
            int channel, const QString &mcc, const QString &mnc);
    CellBroadcastAttentionProfile emergencyAttentionProfile();
    QVariantMap messagePropertiesForChannel(
            int channel, const QString &mcc, const QString &mnc);

    void refresh();
    void setChannelEnabled(const QString &categoryId,
                           const QString &scope,
                           bool enabled);
    void removeUnknownTopic(const QString &topics);
    void resetToRecommended();

Q_SIGNALS:
    void catalogPathChanged();
    void modemPathChanged();
    void imsiChanged();
    void simMccChanged();
    void simMncChanged();
    void networkMccChanged();
    void networkMncChanged();
    void networkStatusChanged();
    void emergencyModeChanged();
    void alertsEnabledChanged();
    void availableChanged();
    void errorStringChanged();
    void channelsChanged();
    void incomingBroadcast(const QString &text, int channel);
    void incomingBroadcastWithProperties(const QString &text,
                                         const QVariantMap &properties);
    void emergencyBroadcast(const QString &text, const QVariantMap &properties);

private Q_SLOTS:
    void onAlertsEnabledSettingChanged();
    void onCellBroadcastEnabledChanged(bool enabled);
    void onCellBroadcastTopicsChanged(const QString &topics);
    void onCellBroadcastValidChanged(bool valid);

private:
    friend class CellBroadcastOfonoClient;

    struct ActiveCatalogEntry;
    struct ResolvedState;
    enum PendingPropertyOperation {
        NoPropertyOperation,
        SetPoweredOperation,
        SetTopicsOperation
    };

    void ensureCatalog();
    void recalculate();
    ActiveCatalogEntry activeEntry() const;
    ActiveCatalogEntry activeEntryForPlmn(const QString &mcc,
                                          const QString &mnc) const;
    ResolvedState resolve() const;
    QString identity() const;
    bool isRoamingNetworkRelevant() const;
    bool channelEnabled(const QString &scope,
                        const CellBroadcastCatalogCategory &category) const;
    MDConfItem *settingItem(const QString &key) const;
    CellBroadcastTopicRangeList lastManagedTopics() const;
    void setLastManagedTopics(const CellBroadcastTopicRangeList &topics);
    void setAlertsEnabledValue(bool enabled);
    void processTopicUpdate();
    void updateCurrentTopics(const QString &topics);
    void cellBroadcastInterfaceDropped();
    void cellBroadcastPropertiesFinished(const QString &errorString);
    void cellBroadcastPropertySetFinished(const QString &property,
                                          const QString &errorString);
    QVariantMap channelMap(const CellBroadcastCatalogCategory &category,
                           const QString &alertSystem,
                           const QString &scope,
                           const QString &plmn,
                           bool roaming,
                           const CellBroadcastTopicRangeList &ranges,
                           bool enabled) const;
    void setErrorString(const QString &errorString);
    void setAvailable(bool available);

private:
    QString m_catalogPath;
    QString m_modemPath;
    QString m_imsi;
    QString m_simMcc;
    QString m_simMnc;
    QString m_networkMcc;
    QString m_networkMnc;
    QString m_networkStatus;
    bool m_emergencyMode;
    bool m_alertsEnabled;
    bool m_available;
    bool m_catalogLoaded;
    bool m_haveTopics;
    bool m_refreshPending;
    bool m_resetRequested;
    PendingPropertyOperation m_pendingPropertyOperation;
    QString m_errorString;
    MDConfItem *m_alertsEnabledItem;
    CellBroadcastOfonoClient *m_cellBroadcast;
    mutable QHash<QString, MDConfItem *> m_settingItems;
    CellBroadcastCatalog m_catalog;
    CellBroadcastTopicRangeList m_currentTopics;
    CellBroadcastTopicRangeList m_requestedTopicRemovals;
    CellBroadcastTopicRangeList m_inFlightManagedTopics;
    QTimer *m_retryTimer;
    QVariantList m_mandatoryChannels;
    QVariantList m_optionalChannels;
    QVariantList m_unknownTopics;
};

#endif
