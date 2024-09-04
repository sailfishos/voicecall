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

using namespace VoiceCall;

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
        FILTER_NO_MATCH,
        FILTER_APPROVE,
        FILTER_IGNORE,
        FILTER_REJECT
    };

    Action apply(const QString &number) const
    {
        // Give priority to exact matching.
        if (m_whitelist.exactMatch(number)) {
            return FILTER_APPROVE;
        } else if (m_rejected.exactMatch(number)) {
            return FILTER_REJECT;
        } else if (m_ignored.exactMatch(number)) {
            return FILTER_IGNORE;
        } else if (m_whitelist.match(number)) {
            return FILTER_APPROVE;
        } else if (m_rejected.match(number)) {
            return FILTER_REJECT;
        } else if (m_ignored.match(number)) {
            return FILTER_IGNORE;
        } else {
            return FILTER_NO_MATCH;
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

void Filter::ignoreNumber(const QString &number)
{
    d->m_whitelist.removeEntry(number);
    d->m_rejected.removeEntry(number);
    d->m_ignored.addEntry(number);
}

void Filter::ignoreNumbersStartingWith(const QString &prefix)
{
    QString pattern = prefix;
    ignoreNumber(pattern.prepend('^'));
}

void Filter::ignoreByDefault()
{
    ignoreNumber(QStringLiteral("*"));
}

void Filter::rejectNumber(const QString &number)
{
    d->m_whitelist.removeEntry(number);
    d->m_ignored.removeEntry(number);
    d->m_rejected.addEntry(number);
}

void Filter::rejectNumbersStartingWith(const QString &prefix)
{
    QString pattern = prefix;
    rejectNumber(pattern.prepend('^'));
}

void Filter::rejectByDefault()
{
    rejectNumber(QStringLiteral("*"));
}

void Filter::acceptNumber(const QString &number)
{
    d->m_rejected.removeEntry(number);
    d->m_ignored.removeEntry(number);
    if (d->m_rejected.match(number)
        || d->m_ignored.match(number)) {
        // Whitelist a number only if it is blocked by a pattern.
        d->m_whitelist.addEntry(number);
    }
}

void Filter::acceptNumbersStartingWith(const QString &prefix)
{
    QString pattern = prefix;
    pattern.prepend('^');
    d->m_ignored.removeEntry(pattern);
    d->m_rejected.removeEntry(pattern);
    if (d->m_rejected.match(prefix)
        || d->m_ignored.match(prefix)) {
        // Whitelist a prefix only if it is blocked by a larger pattern.
        d->m_whitelist.addEntry(pattern);
    }
}

void Filter::acceptByDefault()
{
    d->m_ignored.removeEntry(QStringLiteral("*"));
    d->m_rejected.removeEntry(QStringLiteral("*"));
}

void Filter::acceptAll()
{
    d->m_ignored.clear();
    d->m_rejected.clear();
    d->m_whitelist.clear();
}

bool Filter::isIgnored(const QString &number) const
{
    return (d->apply(number) == Private::FILTER_IGNORE);
}

bool Filter::isRejected(const QString &number) const
{
    return (d->apply(number) == Private::FILTER_REJECT);
}

bool Filter::isAccepted(const QString &number) const
{
    Private::Action action = d->apply(number);
    return (action == Private::FILTER_APPROVE || action == Private::FILTER_NO_MATCH);
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
