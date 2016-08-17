/*
 * This file is a part of the Voice Call Manager project
 *
 * Copyright (C) 2016 Jolla Ltd.
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
#ifndef BASE_CHANNEL_HANDLER_H
#define BASE_CHANNEL_HANDLER_H

#include <abstractvoicecallhandler.h>

#include <TelepathyQt/Channel>

class TelepathyProvider;

class BaseChannelHandler : public AbstractVoiceCallHandler
{
    Q_OBJECT

public:
    explicit BaseChannelHandler(QObject *parent = 0);

    virtual Tp::ChannelPtr channel() const = 0;
    virtual void setParentHandlerId(const QString &handler) = 0;
    virtual void addChildCall(BaseChannelHandler *handler) = 0;
    virtual void removeChildCall(BaseChannelHandler *handler) = 0;

Q_SIGNALS:
    /*** StreamedMediaChannelHandler Implementation ***/
    void error(const QString &errorMessage);
    void invalidated(const QString &errorName, const QString &errorMessage);

    void channelMerged(Tp::ChannelPtr channel);
    void channelRemoved(Tp::ChannelPtr channel);

private:
    Q_DISABLE_COPY(BaseChannelHandler)
};

#endif // BASE_CHANNEL_HANDLER_H
