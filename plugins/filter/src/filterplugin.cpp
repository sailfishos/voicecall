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

#include "filterplugin.h"
#include "filter.h"

#include <voicecallmanagerinterface.h>
#include <abstractvoicecallhandler.h>

class FilterPlugin::Private
{
public:
    Private()
    {
    }

    VoiceCallManagerInterface *m_manager = nullptr;
    VoiceCall::Filter m_filter;
};

FilterPlugin::FilterPlugin(QObject *parent)
    : AbstractVoiceCallManagerPlugin(parent), d(new Private)
{
}

FilterPlugin::~FilterPlugin()
{
}

QString FilterPlugin::pluginId() const
{
    return PLUGIN_NAME;
}

bool FilterPlugin::initialize()
{
    return true;
}

bool FilterPlugin::configure(VoiceCallManagerInterface *manager)
{
    d->m_manager = manager;

    return true;
}

bool FilterPlugin::start()
{
    return resume();
}

bool FilterPlugin::suspend()
{
    d->m_manager->setCallFiltering(false);
    disconnect(d->m_manager, &VoiceCallManagerInterface::voiceCallAdded,
               this, &FilterPlugin::onVoiceCallAdded);
    return true;
}

bool FilterPlugin::resume()
{
    d->m_manager->setCallFiltering();
    connect(d->m_manager, &VoiceCallManagerInterface::voiceCallAdded,
            this, &FilterPlugin::onVoiceCallAdded);
    return true;
}

void FilterPlugin::finalize()
{
    suspend();
}

void FilterPlugin::onVoiceCallAdded(AbstractVoiceCallHandler *handler)
{
    if (!handler || !handler->isIncoming()) {
        return;
    }

    handler->filter(d->m_filter.evaluate(*handler));
}
