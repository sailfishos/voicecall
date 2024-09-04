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
        , m_whitelist(QString::fromLatin1("/sailfish/voicecall/filter/whitelist"))
    {
    }

    enum Action {
          NO_FILTER,
          FILTER_IGNORE,
          FILTER_REJECT
    };

    Action apply(const QString &number) const
    {
        // Give priority to exact matching.
        if (m_whitelist.exactMatch(number)) {
            return NO_FILTER;
        } else if (m_rejected.exactMatch(number)) {
            return FILTER_REJECT;
        } else if (m_ignored.exactMatch(number)) {
            return FILTER_IGNORE;
        } else if (m_whitelist.match(number)) {
            return NO_FILTER;
        } else if (m_rejected.match(number)) {
            return FILTER_REJECT;
        } else if (m_ignored.match(number)) {
            return FILTER_IGNORE;
        } else {
            return NO_FILTER;
        }
    }

    FilterList m_rejected;
    FilterList m_ignored;
    FilterList m_whitelist;
};

Filter::Filter(QObject *parent)
    : QObject(parent)
    , d(new Private)
{
    connect(&d->m_rejected, &FilterList::changed,
            this, &Filter::rejectedListChanged);
    connect(&d->m_ignored, &FilterList::changed,
            this, &Filter::ignoredListChanged);
    connect(&d->m_whitelist, &FilterList::changed,
            this, &Filter::whiteListChanged);
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

QStringList Filter::whiteList() const
{
    return d->m_whitelist.list();
}

AbstractVoiceCallHandler::VoiceCallFilterAction Filter::evaluate(const AbstractVoiceCallHandler &incomingCall) const
{
    switch (d->apply(incomingCall.lineId())) {
    case Private::FILTER_IGNORE:
        return AbstractVoiceCallHandler::ACTION_IGNORE;
    case Private::FILTER_REJECT:
        return AbstractVoiceCallHandler::ACTION_REJECT;
    default:
        return AbstractVoiceCallHandler::ACTION_CONTINUE;
    }
}
