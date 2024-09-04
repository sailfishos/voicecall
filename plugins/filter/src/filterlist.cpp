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

#include "filterlist.h"

#include <common.h>
#include <CommHistory/commonutils.h>

#include <MGConfItem>

class FilterList::Private
{
public:
    Private(const QString &key)
        : m_conf(key)
    {
    }

    MGConfItem m_conf;
};

FilterList::FilterList(const QString &key, QObject *parent)
    : QObject(parent)
    , d(new Private(key))
{
    connect(&d->m_conf, &MGConfItem::valueChanged,
            this, &FilterList::changed);
}

FilterList::~FilterList()
{
}

QString FilterList::key() const
{
    return d->m_conf.key();
}

QStringList FilterList::list() const
{
    return d->m_conf.value().toStringList();
}

void FilterList::set(const QStringList &list)
{
    d->m_conf.set(list);
}

bool FilterList::match(const QString &number) const
{
    for (const QString &filter : list()) {
        if (filter.startsWith('+') || filter[0].isDigit()) {
            // Exact number matching
            if (number == filter
                || CommHistory::remoteAddressMatch(CommHistory::RING_ACCOUNT, number, filter, true)) {
                return true;
            }
        } else if (filter.startsWith('^')) {
            // Prefix matching
            if (number.startsWith(filter.mid(1))) {
                return true;
            }
        } else if (filter == QStringLiteral("*")) {
            // All
            return true;
        } else {
            qCWarning(voicecall) << "unknown filter" << filter;
        }
    }
    return false;
}

bool FilterList::exactMatch(const QString &number) const
{
    return list().contains(number);
}
