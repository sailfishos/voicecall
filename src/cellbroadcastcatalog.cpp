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

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>

namespace {

const char DefaultCatalogPath[] = "/usr/share/cell-broadcast-provider-info/channels.json";
const int MaximumVibrationPatternLength = 64;
const int MaximumVibrationStepDuration = 10000;

QList<int> vibrationPattern(const QJsonValue &value)
{
    const QJsonArray array = value.toArray();
    if (array.size() < 2 || array.size() > MaximumVibrationPatternLength) {
        return QList<int>();
    }

    QList<int> pattern;
    pattern.reserve(array.size());
    for (int index = 0; index < array.size(); ++index) {
        if (!array.at(index).isDouble()) {
            return QList<int>();
        }
        const int duration = array.at(index).toInt(-1);
        const int minimum = index == 0 ? 0 : 1;
        if (duration < minimum || duration > MaximumVibrationStepDuration) {
            return QList<int>();
        }
        pattern.append(duration);
    }
    return pattern;
}


bool validOverlayEntry(const QJsonObject &entry, const QJsonObject &root)
{
    const auto validReference = [&root](const QJsonObject &object,
                                        const QString &field, const QString &table) {
        return !object.contains(field) || (object.value(field).isString()
                && root.value(table).toObject().contains(object.value(field).toString()));
    };
    if (!validReference(entry, QStringLiteral("defaultAttentionProfile"), QStringLiteral("attentionProfiles"))
            || !validReference(entry, QStringLiteral("defaultVibrationProfile"), QStringLiteral("vibrationProfiles"))) {
        return false;
    }
    const QJsonArray categories = entry.value(QStringLiteral("categories")).toArray();
    if (categories.isEmpty()) {
        return false;
    }
    QSet<QString> ids;
    for (const QJsonValue &value : categories) {
        const QJsonObject category = value.toObject();
        const QString id = category.value(QStringLiteral("id")).toString();
        if (id.isEmpty() || ids.contains(id)
                || !validReference(category, QStringLiteral("attentionProfile"), QStringLiteral("attentionProfiles"))
                || !validReference(category, QStringLiteral("sourceRef"), QStringLiteral("regulatorySources"))) {
            return false;
        }
        ids.insert(id);
        for (const QString &key : { QStringLiteral("attentionMode"), QStringLiteral("languageFilter") }) {
            if (category.contains(key) && !category.value(key).isString()) {
                return false;
            }
        }
        const QString mode = category.value(QStringLiteral("attentionMode")).toString(QStringLiteral("warning"));
        const QString filter = category.value(QStringLiteral("languageFilter")).toString(QStringLiteral("device"));
        if ((mode != QLatin1String("warning") && mode != QLatin1String("silent") && mode != QLatin1String("sms"))
                || (filter != QLatin1String("device") && filter != QLatin1String("none"))) {
            return false;
        }
        const QJsonValue duration = category.value(QStringLiteral("attentionDurationMs"));
        if (!duration.isUndefined() && (!duration.isDouble() || duration.toDouble() < 0
                || duration.toDouble() > 600000 || duration.toDouble() != duration.toInt())) {
            return false;
        }
        const QStringList booleans = { QStringLiteral("attentionRepeat"), QStringLiteral("defaultEnabled"),
            QStringLiteral("userConfigurable"), QStringLiteral("settingsVisible"), QStringLiteral("customName"),
            QStringLiteral("vibrationRepeat") };
        for (const QString &key : booleans) {
            if (category.contains(key) && !category.value(key).isBool()) {
                return false;
            }
        }
        const QJsonValue translations = category.value(QStringLiteral("translations"));
        if (!translations.isUndefined() && !translations.isObject()) {
            return false;
        }
        const QJsonObject locales = translations.toObject();
        const QRegularExpression language(QStringLiteral("^[a-z]{2,3}(?:-[a-z0-9]+)*$"));
        for (auto it = locales.begin(); it != locales.end(); ++it) {
            if (!language.match(it.key()).hasMatch() || !it.value().isObject()) {
                return false;
            }
            const QJsonObject fields = it.value().toObject();
            for (auto field = fields.begin(); field != fields.end(); ++field) {
                if (!field.value().isString() || (field.key() != QLatin1String("name")
                        && field.key() != QLatin1String("title") && field.key() != QLatin1String("description"))) {
                    return false;
                }
            }
        }
        const QJsonArray ranges = category.value(QStringLiteral("ranges")).toArray();
        if (ranges.isEmpty()) {
            return false;
        }
        for (const QJsonValue &rangeValue : ranges) {
            const QJsonObject range = rangeValue.toObject();
            const QJsonValue from = range.value(QStringLiteral("from"));
            const QJsonValue to = range.value(QStringLiteral("to"));
            if (!from.isDouble() || !to.isDouble() || from.toDouble() != from.toInt()
                    || to.toDouble() != to.toInt() || from.toInt() < 0
                    || to.toInt() > 65535 || from.toInt() > to.toInt()) {
                return false;
            }
            for (const QString &key : { QStringLiteral("mandatory"), QStringLiteral("apply") }) {
                if (range.contains(key) && !range.value(key).isBool()) {
                    return false;
                }
            }
        }
    }
    return true;
}

bool mergeOverlays(const QString &catalogPath, QJsonObject *root, QString *error)
{
    const QString directoryPath = QFileInfo(catalogPath).absolutePath() + QStringLiteral("/overrides.d");
    const QFileInfo directoryInfo(directoryPath);
    if (!directoryInfo.exists()) {
        return true;
    }
    if (!directoryInfo.isDir() || !directoryInfo.isReadable()) {
        *error = QStringLiteral("Cannot read catalog overlay directory: ") + directoryPath;
        return false;
    }
    const QDir directory(directoryPath);
    const QStringList files = directory.entryList(QStringList() << QStringLiteral("*.json"),
                                                  QDir::Files, QDir::Name);
    for (const QString &name : files) {
        QFile file(directory.filePath(name));
        *error = QStringLiteral("Invalid catalog overlay: ") + file.fileName();
        if (!file.open(QIODevice::ReadOnly)) {
            *error += QStringLiteral(": ") + file.errorString();
            return false;
        }
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
        const QJsonObject overlay = document.object();
        if (parseError.error != QJsonParseError::NoError || !document.isObject()
                || overlay.value(QStringLiteral("version")).toDouble() != 1
                || !overlay.value(QStringLiteral("sources")).isObject()
                || !overlay.value(QStringLiteral("entries")).isObject()) {
            return false;
        }
        for (const QString &key : overlay.keys()) {
            if (key != QLatin1String("version") && key != QLatin1String("sources")
                    && key != QLatin1String("entries")) {
                return false;
            }
        }
        QJsonObject sources = root->value(QStringLiteral("regulatorySources")).toObject();
        const QJsonObject addedSources = overlay.value(QStringLiteral("sources")).toObject();
        for (auto it = addedSources.begin(); it != addedSources.end(); ++it) {
            if (!it.value().isObject() || (sources.contains(it.key()) && sources.value(it.key()) != it.value())) {
                return false;
            }
            sources.insert(it.key(), it.value());
        }
        root->insert(QStringLiteral("regulatorySources"), sources);
        QJsonObject entries = root->value(QStringLiteral("entries")).toObject();
        const QJsonObject overrides = overlay.value(QStringLiteral("entries")).toObject();
        const QRegularExpression plmnPattern(QStringLiteral("^[0-9]{3}(?:[0-9]{2,3})?$"));
        // QJsonObject iteration is sorted: MCC replacement precedes its PLMN exceptions.
        for (auto it = overrides.begin(); it != overrides.end(); ++it) {
            QJsonObject entry = it.value().toObject();
            if (!plmnPattern.match(it.key()).hasMatch() || it.key() == QLatin1String("001")
                    || (entry.contains(QStringLiteral("plmn")) && entry.value(QStringLiteral("plmn")).toString() != it.key())
                    || !validOverlayEntry(entry, *root)) {
                return false;
            }
            if (it.key().size() == 3) {
                for (const QString &key : entries.keys()) {
                    if (key.startsWith(it.key())) {
                        entries.remove(key);
                    }
                }
            }
            entry.insert(QStringLiteral("plmn"), it.key());
            entries.insert(it.key(), entry);
        }
        root->insert(QStringLiteral("entries"), entries);
    }
    error->clear();
    return true;
}

}

bool CellBroadcastCatalogEntry::isValid() const
{
    return !categories.isEmpty();
}

bool CellBroadcastVibrationProfile::isValid() const
{
    return !id.isEmpty() && !vibrationPattern.isEmpty();
}

bool CellBroadcastAttentionProfile::isValid() const
{
    return !id.isEmpty() && !soundFile.isEmpty();
}

QString CellBroadcastAttentionProfile::hapticSequence() const
{
    QStringList steps;
    for (int index = 0; index < vibrationPattern.size(); ++index) {
        const int duration = vibrationPattern.at(index);
        if (index == 0 && duration == 0) {
            continue;
        }
        steps.append((index % 2 ? QStringLiteral("on=") : QStringLiteral("pause="))
                     + QString::number(duration));
    }
    if (vibrationRepeat && !steps.isEmpty()) {
        steps.append(QStringLiteral("repeat=forever"));
    }
    return steps.join(QLatin1Char(','));
}

CellBroadcastCatalog::CellBroadcastCatalog()
    : m_valid(false)
{
}

bool CellBroadcastCatalog::load(const QString &path)
{
    m_entries.clear();
    m_attentionProfiles.clear();
    m_vibrationProfiles.clear();
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

    QJsonObject root = document.object();
    if (!mergeOverlays(catalogPath, &root, &m_errorString)) {
        m_valid = false;
        return false;
    }
    m_sourceCommit = root.value(QStringLiteral("source")).toObject()
            .value(QStringLiteral("commit")).toString();

    const QJsonObject vibrationProfiles = root.value(
                QStringLiteral("vibrationProfiles")).toObject();
    for (auto profileIt = vibrationProfiles.begin();
         profileIt != vibrationProfiles.end(); ++profileIt) {
        const QJsonObject profileObject = profileIt.value().toObject();
        CellBroadcastVibrationProfile profile;
        profile.id = profileIt.key();
        profile.vibrationPattern = vibrationPattern(
                    profileObject.value(QStringLiteral("vibrationPattern")));
        profile.vibrationRepeat = profileObject.value(
                    QStringLiteral("vibrationRepeat")).toBool(false);
        if (profile.isValid()) {
            m_vibrationProfiles.insert(profile.id, profile);
        }
    }

    const QJsonObject attentionProfiles = root.value(QStringLiteral("attentionProfiles")).toObject();
    for (auto profileIt = attentionProfiles.begin(); profileIt != attentionProfiles.end(); ++profileIt) {
        const QJsonObject profileObject = profileIt.value().toObject();
        CellBroadcastAttentionProfile profile;
        profile.id = profileIt.key();
        profile.event = profileObject.value(QStringLiteral("event")).toString();
        profile.soundFile = profileObject.value(QStringLiteral("soundFile")).toString();
        profile.reservedUse = profileObject.value(QStringLiteral("reservedUse")).toString();
        profile.vibrationProfile = profileObject.value(
                    QStringLiteral("vibrationProfile")).toString();
        profile.vibrationPattern = vibrationPattern(
                    profileObject.value(QStringLiteral("vibrationPattern")));
        profile.vibrationRepeat = profileObject.value(
                    QStringLiteral("vibrationRepeat")).toBool(false);
        const CellBroadcastVibrationProfile namedVibrationProfile =
                m_vibrationProfiles.value(profile.vibrationProfile);
        if (namedVibrationProfile.isValid()) {
            profile.vibrationPattern = namedVibrationProfile.vibrationPattern;
            profile.vibrationRepeat = namedVibrationProfile.vibrationRepeat;
        }
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
        entry.defaultVibrationProfile = entryObject.value(
                    QStringLiteral("defaultVibrationProfile")).toString();
        entry.defaultVibrationPattern = vibrationPattern(
                    entryObject.value(QStringLiteral("defaultVibrationPattern")));

        const QJsonArray categories = entryObject.value(QStringLiteral("categories")).toArray();
        for (const QJsonValue &categoryValue : categories) {
            const QJsonObject categoryObject = categoryValue.toObject();
            CellBroadcastCatalogCategory category;
            category.id = categoryObject.value(QStringLiteral("id")).toString();
            category.name = categoryObject.value(QStringLiteral("name")).toString();
            category.title = categoryObject.value(QStringLiteral("title")).toString(category.name);
            category.description = categoryObject.value(QStringLiteral("description")).toString();
            category.translations = categoryObject.value(
                        QStringLiteral("translations")).toObject().toVariantMap();
            category.attentionMode = categoryObject.value(QStringLiteral("attentionMode")).toString();
            if (category.attentionMode != QLatin1String("silent")
                    && category.attentionMode != QLatin1String("sms")) {
                category.attentionMode = QStringLiteral("warning");
            }
            category.attentionDurationMs = qBound(0, categoryObject.value(
                        QStringLiteral("attentionDurationMs")).toInt(), 600000);
            category.attentionRepeat = categoryObject.value(
                        QStringLiteral("attentionRepeat")).toBool(true);
            category.languageFilter = categoryObject.value(QStringLiteral("languageFilter")).toString();
            if (category.languageFilter != QLatin1String("none")) {
                category.languageFilter = QStringLiteral("device");
            }
            category.alertLevel = categoryObject.value(QStringLiteral("alertLevel")).toString();
            category.attentionProfile = categoryObject.value(QStringLiteral("attentionProfile")).toString();
            category.attentionPolicy = categoryObject.value(QStringLiteral("attentionPolicy")).toString();
            category.display = categoryObject.value(QStringLiteral("display")).toString(
                        QStringLiteral("alert"));
            category.sourceRef = categoryObject.value(QStringLiteral("sourceRef")).toString();
            category.customName = categoryObject.value(QStringLiteral("customName")).toBool(false);
            category.defaultEnabled = categoryObject.value(QStringLiteral("defaultEnabled")).toBool(true);
            category.userConfigurable = categoryObject.value(QStringLiteral("userConfigurable")).toBool(true);
            category.settingsVisible = categoryObject.value(QStringLiteral("settingsVisible")).toBool(true);
            category.vibrationPattern = vibrationPattern(
                        categoryObject.value(QStringLiteral("vibrationPattern")));
            category.hasVibrationRepeat = categoryObject.contains(
                        QStringLiteral("vibrationRepeat"));
            category.vibrationRepeat = categoryObject.value(
                        QStringLiteral("vibrationRepeat")).toBool(false);

            const QJsonArray ranges = categoryObject.value(QStringLiteral("ranges")).toArray();
            for (const QJsonValue &rangeValue : ranges) {
                const QJsonObject rangeObject = rangeValue.toObject();
                CellBroadcastCatalogRange range;
                range.from = rangeObject.value(QStringLiteral("from")).toInt();
                range.to = rangeObject.value(QStringLiteral("to")).toInt();
                range.mandatory = rangeObject.value(QStringLiteral("mandatory")).toBool();
                range.apply = rangeObject.value(QStringLiteral("apply")).toBool(true);
                range.languageRole = rangeObject.value(QStringLiteral("languageRole")).toString();
                range.vibrationPattern = vibrationPattern(
                            rangeObject.value(QStringLiteral("vibrationPattern")));
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

CellBroadcastVibrationProfile CellBroadcastCatalog::vibrationProfile(
        const QString &id) const
{
    return m_vibrationProfiles.value(id);
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
