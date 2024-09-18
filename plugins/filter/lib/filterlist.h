/*
 * This file is a part of the Voice Call Manager project
 *
 * Copyright (C) 2024  Damien Caliste <dcaliste@free.fr>
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

#ifndef MATCHER_H
#define MATCHER_H

#include <QSharedPointer>
#include <QObject>

#include <CommHistory/Recipient>

class FilterList : public QObject
{
    Q_OBJECT
public:
    FilterList(const QString &key, QObject *parent = nullptr);
    ~FilterList();

    bool match(const CommHistory::Recipient &recipient) const;
    bool exactMatch(const QString &number) const;
    QString key() const;
    QStringList list() const;
    void set(const QStringList &list);
    void addEntry(const QString &entry);
    void removeEntry(const QString &entry);
    void clear();

signals:
    void changed();

private:
    class Private;
    QSharedPointer<Private> d;
};

#endif
