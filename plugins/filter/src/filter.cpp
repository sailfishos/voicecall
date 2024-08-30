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
        : m_rejected(QString::fromLatin1("/sailfish/voicecall/filter/rejected-numbers"))
        , m_ignored(QString::fromLatin1("/sailfish/voicecall/filter/ignored-numbers"))
    {
    }

    FilterList m_rejected;
    FilterList m_ignored;
};

Filter::Filter(QObject *parent)
    : QObject(parent)
    , d(new Private)
{
    connect(&d->m_rejected, &FilterList::changed,
            this, &Filter::rejectedListChanged);
    connect(&d->m_ignored, &FilterList::changed,
            this, &Filter::ignoredListChanged);
}

Filter::~Filter()
{
}

QStringList Filter::rejectedList() const
{
    return d->m_rejected.list();
}

QStringList Filter::ignoredList() const
{
    return d->m_ignored.list();
}

AbstractVoiceCallHandler::VoiceCallFilterAction Filter::evaluate(const AbstractVoiceCallHandler &incomingCall) const
{
    const QString incomingNumber = incomingCall.lineId();
    if (d->m_rejected.match(incomingNumber)) {
        return AbstractVoiceCallHandler::ACTION_REJECT;
    } else if (d->m_ignored.match(incomingNumber)) {
        return AbstractVoiceCallHandler::ACTION_IGNORE;
    } else {
        return AbstractVoiceCallHandler::ACTION_CONTINUE;
    }
}
