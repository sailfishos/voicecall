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

#ifndef FILTER_H
#define FILTER_H

#include <QSharedPointer>
#include <QObject>

#include <abstractvoicecallhandler.h>

#include <QtCore/QtGlobal>

#if defined(FILTER_SHARED)
#  define FILTER_EXPORT Q_DECL_EXPORT
#else
#  define FILTER_EXPORT Q_DECL_IMPORT
#endif

namespace VoiceCall {

class FILTER_EXPORT Filter : public QObject
{
    Q_OBJECT
public:
    Filter(QObject *parent = nullptr);
    ~Filter();

    QStringList ignoredList() const;
    QStringList rejectedList() const;
    QStringList whiteList() const;

    void ignoreNumber(const QString &number);
    void ignoreNumbersStartingWith(const QString &prefix);
    void ignoreByDefault();

    void rejectNumber(const QString &number);
    void rejectNumbersStartingWith(const QString &prefix);
    void rejectByDefault();

    void acceptNumber(const QString &number);
    void acceptNumbersStartingWith(const QString &prefix);
    void acceptByDefault();

    void acceptAll();

    bool isIgnored(const QString &number) const;
    bool isRejected(const QString &number) const;
    bool isAccepted(const QString &number) const;

    AbstractVoiceCallHandler::VoiceCallFilterAction evaluate(const AbstractVoiceCallHandler &incomingCall) const;

signals:
    void ignoredListChanged();
    void rejectedListChanged();
    void whiteListChanged();

private:
    class Private;
    QSharedPointer<Private> d;
};

}

#endif
