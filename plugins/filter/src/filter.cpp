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

#include "filter.h"
#include "filterlist.h"

class Filter::Private
{
public:
    Private()
        : m_blocked(QString::fromLatin1("/sailfish/voicecall/filter/blocked-numbers"))
    {
    }

    FilterList m_blocked;
};

Filter::Filter(QObject *parent)
    : QObject(parent)
    , d(new Private)
{
    connect(&d->m_blocked, &FilterList::changed,
            this, &Filter::blockedListChanged);
}

Filter::~Filter()
{
}

QStringList Filter::blockedList() const
{
    return d->m_blocked.list();
}

Filter::Action Filter::evaluate(const QString &incomingNumber) const
{
    if (d->m_blocked.match(incomingNumber)) {
        return Block;
    } else {
        return Continue;
    }
}
