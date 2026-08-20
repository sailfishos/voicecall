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
#ifndef CELLBROADCASTCATALOG_H
#define CELLBROADCASTCATALOG_H

#include "cellbroadcasttopics.h"

#include <QHash>
#include <QList>
#include <QString>

struct CellBroadcastCatalogRange
{
    int from;
    int to;
    bool mandatory;
    bool apply;
    QString languageRole;
};

struct CellBroadcastCatalogCategory
{
    QString id;
    QString name;
    QString title;
    QString alertLevel;
    QString attentionProfile;
    QString attentionPolicy;
    QString display;
    QString sourceRef;
    bool customName;
    bool defaultEnabled;
    bool userConfigurable;
    bool settingsVisible;
    QList<CellBroadcastCatalogRange> ranges;
};

struct CellBroadcastAttentionProfile
{
    QString id;
    QString event;
    QString soundFile;
    QString reservedUse;

    bool isValid() const;
};

struct CellBroadcastCatalogEntry
{
    QString plmn;
    QString alertSystem;
    QString defaultAttentionProfile;
    QList<CellBroadcastCatalogCategory> categories;

    bool isValid() const;
};

class CellBroadcastCatalog
{
public:
    CellBroadcastCatalog();

    bool load(const QString &path = QString());
    bool isValid() const;
    QString errorString() const;
    QString sourceCommit() const;

    CellBroadcastAttentionProfile attentionProfile(const QString &id) const;
    CellBroadcastCatalogEntry configuredEntryForPlmn(const QString &mcc, const QString &mnc) const;
    CellBroadcastCatalogEntry entryForPlmn(const QString &mcc, const QString &mnc) const;
    CellBroadcastCatalogEntry entryForKey(const QString &plmn) const;

private:
    QHash<QString, CellBroadcastAttentionProfile> m_attentionProfiles;
    QHash<QString, CellBroadcastCatalogEntry> m_entries;
    QString m_sourceCommit;
    QString m_errorString;
    bool m_valid;
};

#endif
