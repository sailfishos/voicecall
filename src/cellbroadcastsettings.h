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
#ifndef CELLBROADCASTSETTINGS_H
#define CELLBROADCASTSETTINGS_H

/*
 * The cell broadcast settings are dconf keys on Sailfish OS, reached through
 * mlite's MDConfItem. mlite is part of the Nemo stack and is not packaged
 * everywhere, so this header hands out a CellBroadcastSettingItem that is
 * MDConfItem where it exists, and a QSettings-backed stand-in with the same
 * small API where it does not.
 */

#ifdef HAVE_MLITE

#include <MDConfItem>

typedef MDConfItem CellBroadcastSettingItem;

#else

#include <QObject>
#include <QString>
#include <QVariant>

/*
 * Only the parts of MDConfItem that the cell broadcast code uses. Keys keep
 * their dconf spelling ("/apps/sailfish/cellbroadcast/enabled") so the two
 * backends stay interchangeable; the leading slash is dropped when the key is
 * handed to QSettings.
 *
 * Unlike dconf this store is per-process: a change made by another process is
 * not noticed. That is enough here because voicecall-manager owns these keys
 * and everyone else goes through its cell broadcast D-Bus interface.
 */
class CellBroadcastSettingItem : public QObject
{
    Q_OBJECT

public:
    explicit CellBroadcastSettingItem(const QString &key, QObject *parent = 0);
    ~CellBroadcastSettingItem();

    QString key() const;
    QVariant value() const;
    QVariant value(const QVariant &def) const;
    void set(const QVariant &val);
    void unset();

Q_SIGNALS:
    void valueChanged();

private:
    const QString m_key;
};

#endif // HAVE_MLITE

#endif // CELLBROADCASTSETTINGS_H
