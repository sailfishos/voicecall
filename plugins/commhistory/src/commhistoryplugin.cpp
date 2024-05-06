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

#include "commhistoryplugin.h"

#include <common.h>
#include <voicecallmanagerinterface.h>
#include <abstractvoicecallhandler.h>

#include <CommHistory/EventModel>
#include <CommHistory/Event>

#include <QTimer>

class CommHistoryPlugin::Private
{
public:
    Private()
    {
        m_timer.setInterval(5 * 60000);
        connect(&m_timer, &QTimer::timeout,
                [this] () {
                    for (const QString &id : m_calls.keys()) {
                        CommHistory::Event &event = m_calls[id];
                        storeCall(&event);
                    }
                });
    }

    VoiceCallManagerInterface *m_manager = nullptr;
    QMap<QString, CommHistory::Event> m_calls;
    QTimer m_timer;
    CommHistory::EventModel m_eventModel;

    void newVoiceCall(const AbstractVoiceCallHandler &handler)
    {
        CommHistory::Event event;
        event.setType(CommHistory::Event::CallEvent);
        event.setLocalUid(handler.provider()->providerId());
        event.setRecipients(CommHistory::Recipient(event.localUid(),
                                                   handler.lineId()));
        event.setSubscriberIdentity(handler.subscriberId());
        event.setStartTime(QDateTime::currentDateTime());
        event.setEndTime(event.startTime());
        event.setIsEmergencyCall(handler.isEmergency());
        event.setDirection(handler.isIncoming() ? CommHistory::Event::Inbound
                                                : CommHistory::Event::Outbound);
        // Don't store inbound events, since their incoming status
        // will only be known later (libcommhistory expects missed
        // calls to be known on event addition for instance).
        if (event.direction() == CommHistory::Event::Outbound) {
            storeCall(&event);
        }

        if (m_calls.isEmpty()) {
            m_timer.start();
        }
        m_calls.insert(handler.handlerId(), event);
    }

    void onVoiceCallStatusChanged(const AbstractVoiceCallHandler &handler)
    {
        CommHistory::Event &event = m_calls[handler.handlerId()];

        switch (handler.status()) {
        case AbstractVoiceCallHandler::STATUS_ACTIVE:
            event.setStartTime(QDateTime::currentDateTime());
            event.setEndTime(event.startTime());
            storeCall(&event);
            break;
        case AbstractVoiceCallHandler::STATUS_DISCONNECTED:
            event.setEndTime(event.startTime().addSecs(handler.duration()));
            // No need to save the changes now, the voiceCallEnded will do it.
            break;
        case AbstractVoiceCallHandler::STATUS_IGNORED:
            event.setIncomingStatus(CommHistory::Event::Ignored);
            break;
        case AbstractVoiceCallHandler::STATUS_REJECTED:
            event.setIncomingStatus(CommHistory::Event::Rejected);
            break;
        default:
            break;
        }
    }

    void onVoiceCallEmergencyChanged(const AbstractVoiceCallHandler &handler)
    {
        CommHistory::Event &event = m_calls[handler.handlerId()];
        event.setIsEmergencyCall(handler.isEmergency());
        storeCall(&event);
    }

    void onVoiceCallDurationChanged(const AbstractVoiceCallHandler &handler)
    {
        CommHistory::Event &event = m_calls[handler.handlerId()];
        event.setEndTime(event.startTime().addSecs(handler.duration()));
    }

    void storeCall(CommHistory::Event *event)
    {
        // In CommHistory::Event, events are valid when existing in a model.
        if (event->isValid() && !m_eventModel.modifyEvent(*event)) {
            qCWarning(voicecall) << "cannot modify event in call history";
        } else if (!event->isValid() && !m_eventModel.addEvent(*event)) {
            qCWarning(voicecall) << "cannot add event to call history";
        }
    }

    void voiceCallEnded(const QString &id)
    {
        CommHistory::Event event = m_calls.take(id);

        if (event.direction() == CommHistory::Event::Inbound
            && event.incomingStatus() == CommHistory::Event::Received
            && !event.isValid()) {
            event.setIncomingStatus(CommHistory::Event::NotAnswered);
            event.setStartTime(QDateTime::currentDateTime());
            event.setEndTime(event.startTime());
        }
        storeCall(&event);
        if (m_calls.isEmpty()) {
            m_timer.stop();
        }
    }
};

CommHistoryPlugin::CommHistoryPlugin(QObject *parent)
    : AbstractVoiceCallManagerPlugin(parent), d(new Private)
{
}

CommHistoryPlugin::~CommHistoryPlugin()
{
}

QString CommHistoryPlugin::pluginId() const
{
    return PLUGIN_NAME;
}

bool CommHistoryPlugin::initialize()
{
    return true;
}

bool CommHistoryPlugin::configure(VoiceCallManagerInterface *manager)
{
    d->m_manager = manager;

    return true;
}

bool CommHistoryPlugin::start()
{
    return resume();
}

bool CommHistoryPlugin::suspend()
{
    disconnect(d->m_manager, &VoiceCallManagerInterface::voiceCallRemoved,
               this, &CommHistoryPlugin::onVoiceCallRemoved);
    disconnect(d->m_manager, &VoiceCallManagerInterface::voiceCallAdded,
               this, &CommHistoryPlugin::onVoiceCallAdded);
    return true;
}

bool CommHistoryPlugin::resume()
{
    connect(d->m_manager, &VoiceCallManagerInterface::voiceCallAdded,
            this, &CommHistoryPlugin::onVoiceCallAdded);
    connect(d->m_manager, &VoiceCallManagerInterface::voiceCallRemoved,
            this, &CommHistoryPlugin::onVoiceCallRemoved);
    return true;
}

void CommHistoryPlugin::finalize()
{
    suspend();
}

void CommHistoryPlugin::onVoiceCallAdded(AbstractVoiceCallHandler *handler)
{
    if (!handler) {
        return;
    }

    connect(handler, &AbstractVoiceCallHandler::emergencyChanged,
            this, [this, handler] (bool) {
                d->onVoiceCallEmergencyChanged(*handler);
            });
    connect(handler, &AbstractVoiceCallHandler::statusChanged,
            this, [this, handler] (AbstractVoiceCallHandler::VoiceCallStatus) {
                d->onVoiceCallStatusChanged(*handler);
            });
    connect(handler, &AbstractVoiceCallHandler::durationChanged,
            this, [this, handler] (bool) {
                d->onVoiceCallDurationChanged(*handler);
            });

    d->newVoiceCall(*handler);
}

void CommHistoryPlugin::onVoiceCallRemoved(const QString &handlerId)
{
    d->voiceCallEnded(handlerId);
}
