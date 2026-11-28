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
#include "cellbroadcastsettings.h"

#ifndef HAVE_MLITE

#include <QDir>
#include <QGlobalStatic>
#include <QHash>
#include <QList>
#include <QSettings>
#include <QStandardPaths>

namespace {

QString settingsFilePath()
{
    QString base = QString::fromLocal8Bit(qgetenv("VOICECALL_CONFIG_HOME"));
    if (base.isEmpty()) {
        base = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    }
    // voicecall-manager is often started as a system service, which has no HOME
    // for QStandardPaths to work from. Keep the store out of "/" in that case.
    if (base.isEmpty() || !QDir::isAbsolutePath(base) || base == QLatin1String("/")) {
        base = QStringLiteral("/var/lib");
    }
    return base + QStringLiteral("/voicecall/cellbroadcast.conf");
}

QSettings *store()
{
    static QSettings settings(settingsFilePath(), QSettings::IniFormat);
    return &settings;
}

// dconf keys are absolute paths, QSettings keys are relative to the file root.
QString settingsKey(const QString &key)
{
    int start = 0;
    while (start < key.size() && key.at(start) == QLatin1Char('/')) {
        ++start;
    }
    return key.mid(start);
}

typedef QHash<QString, QList<CellBroadcastSettingItem *> > ItemsByKey;
Q_GLOBAL_STATIC(ItemsByKey, itemsByKey)

}

CellBroadcastSettingItem::CellBroadcastSettingItem(const QString &key, QObject *parent)
    : QObject(parent)
    , m_key(key)
{
    (*itemsByKey())[m_key].append(this);
}

CellBroadcastSettingItem::~CellBroadcastSettingItem()
{
    if (!itemsByKey.isDestroyed()) {
        QList<CellBroadcastSettingItem *> &items = (*itemsByKey())[m_key];
        items.removeAll(this);
        if (items.isEmpty()) {
            itemsByKey()->remove(m_key);
        }
    }
}

QString CellBroadcastSettingItem::key() const
{
    return m_key;
}

QVariant CellBroadcastSettingItem::value() const
{
    return store()->value(settingsKey(m_key));
}

QVariant CellBroadcastSettingItem::value(const QVariant &def) const
{
    return store()->value(settingsKey(m_key), def);
}

void CellBroadcastSettingItem::set(const QVariant &val)
{
    if (value() == val) {
        return;
    }

    if (val.isValid()) {
        store()->setValue(settingsKey(m_key), val);
    } else {
        store()->remove(settingsKey(m_key));
    }
    store()->sync();

    Q_EMIT valueChanged();

    // MDConfItem notifies the item that was written to right away and the other
    // items for the same key once the event loop runs. Match that, so callers
    // which set a value from within a valueChanged() handler behave the same.
    const QList<CellBroadcastSettingItem *> items = itemsByKey()->value(m_key);
    for (CellBroadcastSettingItem *item : items) {
        if (item != this) {
            QMetaObject::invokeMethod(item, "valueChanged", Qt::QueuedConnection);
        }
    }
}

void CellBroadcastSettingItem::unset()
{
    set(QVariant());
}

#endif // HAVE_MLITE
