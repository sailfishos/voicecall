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
#include "cellbroadcastcatalog.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace {

const char DefaultCatalogPath[] = "/usr/share/cell-broadcast-provider-info/channels.json";

}

bool CellBroadcastCatalogEntry::isValid() const
{
    return !categories.isEmpty();
}

bool CellBroadcastAttentionProfile::isValid() const
{
    return !id.isEmpty() && !soundFile.isEmpty();
}

CellBroadcastCatalog::CellBroadcastCatalog()
    : m_valid(false)
{
}

bool CellBroadcastCatalog::load(const QString &path)
{
    m_entries.clear();
    m_attentionProfiles.clear();
    m_sourceCommit.clear();

    const QString catalogPath = path.isEmpty()
            ? QString::fromLatin1(DefaultCatalogPath)
            : path;
    QFile file(catalogPath);
    if (!file.open(QIODevice::ReadOnly)) {
        m_errorString = file.errorString();
        m_valid = false;
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        m_errorString = parseError.errorString();
        m_valid = false;
        return false;
    }

    const QJsonObject root = document.object();
    m_sourceCommit = root.value(QStringLiteral("source")).toObject()
            .value(QStringLiteral("commit")).toString();

    const QJsonObject attentionProfiles = root.value(QStringLiteral("attentionProfiles")).toObject();
    for (auto profileIt = attentionProfiles.begin(); profileIt != attentionProfiles.end(); ++profileIt) {
        const QJsonObject profileObject = profileIt.value().toObject();
        CellBroadcastAttentionProfile profile;
        profile.id = profileIt.key();
        profile.soundFile = profileObject.value(QStringLiteral("soundFile")).toString();
        profile.reservedUse = profileObject.value(QStringLiteral("reservedUse")).toString();
        if (profile.isValid()) {
            m_attentionProfiles.insert(profile.id, profile);
        }
    }

    const QJsonObject entries = root.value(QStringLiteral("entries")).toObject();
    for (auto entryIt = entries.begin(); entryIt != entries.end(); ++entryIt) {
        const QJsonObject entryObject = entryIt.value().toObject();
        CellBroadcastCatalogEntry entry;
        entry.plmn = entryObject.value(QStringLiteral("plmn")).toString();
        entry.alertSystem = entryObject.value(QStringLiteral("alertSystem")).toString();
        entry.defaultAttentionProfile = entryObject.value(QStringLiteral("defaultAttentionProfile")).toString();

        const QJsonArray categories = entryObject.value(QStringLiteral("categories")).toArray();
        for (const QJsonValue &categoryValue : categories) {
            const QJsonObject categoryObject = categoryValue.toObject();
            CellBroadcastCatalogCategory category;
            category.id = categoryObject.value(QStringLiteral("id")).toString();
            category.name = categoryObject.value(QStringLiteral("name")).toString();
            category.attentionProfile = categoryObject.value(QStringLiteral("attentionProfile")).toString();
            category.customName = categoryObject.value(QStringLiteral("customName")).toBool(false);
            category.defaultEnabled = categoryObject.value(QStringLiteral("defaultEnabled")).toBool(true);

            const QJsonArray ranges = categoryObject.value(QStringLiteral("ranges")).toArray();
            for (const QJsonValue &rangeValue : ranges) {
                const QJsonObject rangeObject = rangeValue.toObject();
                CellBroadcastCatalogRange range;
                range.from = rangeObject.value(QStringLiteral("from")).toInt();
                range.to = rangeObject.value(QStringLiteral("to")).toInt();
                range.mandatory = rangeObject.value(QStringLiteral("mandatory")).toBool();
                range.apply = rangeObject.value(QStringLiteral("apply")).toBool(true);
                if (range.from <= range.to) {
                    category.ranges.append(range);
                }
            }

            if (!category.id.isEmpty() && !category.ranges.isEmpty()) {
                entry.categories.append(category);
            }
        }

        if (entryIt.key() == QLatin1String("default")
                || entryIt.key() == QLatin1String("001")) {
            m_entries.insert(QString(), entry);
        }
        m_entries.insert(entryIt.key(), entry);
    }

    m_valid = m_entries.contains(QString());
    if (!m_valid) {
        m_errorString = QStringLiteral("Catalog has no default entry");
    } else {
        m_errorString.clear();
    }
    return m_valid;
}

bool CellBroadcastCatalog::isValid() const
{
    return m_valid;
}

QString CellBroadcastCatalog::errorString() const
{
    return m_errorString;
}

QString CellBroadcastCatalog::sourceCommit() const
{
    return m_sourceCommit;
}

CellBroadcastAttentionProfile CellBroadcastCatalog::attentionProfile(const QString &id) const
{
    return m_attentionProfiles.value(id);
}

CellBroadcastCatalogEntry CellBroadcastCatalog::configuredEntryForPlmn(const QString &mcc,
                                                                       const QString &mnc) const
{
    if (!mcc.isEmpty() && !mnc.isEmpty()) {
        const QString exact = mcc + (mnc.length() == 1
                ? mnc.rightJustified(2, QLatin1Char('0'))
                : mnc);
        if (m_entries.contains(exact)) {
            return m_entries.value(exact);
        }
    }
    if (!mcc.isEmpty() && m_entries.contains(mcc)) {
        return m_entries.value(mcc);
    }
    return CellBroadcastCatalogEntry();
}

CellBroadcastCatalogEntry CellBroadcastCatalog::entryForPlmn(const QString &mcc, const QString &mnc) const
{
    const CellBroadcastCatalogEntry entry = configuredEntryForPlmn(mcc, mnc);
    if (entry.isValid()) {
        return entry;
    }
    return m_entries.value(QString());
}

CellBroadcastCatalogEntry CellBroadcastCatalog::entryForKey(const QString &plmn) const
{
    if (m_entries.contains(plmn)) {
        return m_entries.value(plmn);
    }
    if (plmn.length() > 3 && m_entries.contains(plmn.left(3))) {
        return m_entries.value(plmn.left(3));
    }
    return m_entries.value(QString());
}
