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

#include "basechannelhandler.h"

BaseChannelHandler::BaseChannelHandler(QObject *parent)
    : AbstractVoiceCallHandler(parent)
{

}

QString BaseChannelHandler::subscriberId() const
{
    const QVariantMap properties = channel()->immutableProperties();
    return properties.value("SubscriberIdentity").toString();
}

void BaseChannelHandler::filter(VoiceCallFilterAction action)
{
    if (status() != STATUS_NULL) {
        return;
    }

    switch (action) {
    case ACTION_REJECT:
        hangup();
        setStatus(STATUS_REJECTED);
        break;
    case ACTION_IGNORE:
        setStatus(STATUS_IGNORED);
        break;
    default:
        setStatus(STATUS_INCOMING);
        break;
    }
}
