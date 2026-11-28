/*
 * This file is a part of the Voice Call Manager project
 *
 * Copyright (C) 2011-2012  Tom Swindell <t.swindell@rubyx.co.uk>
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
#include <QCoreApplication>

#include "voicecallmanager.h"
#include "basicvoicecallconfigurator.h"
#include "cellbroadcastdaemon.h"
#ifdef HAVE_HEARING_AID_PREFERENCE
#include "hearingaidpreference.h"
#include <ssusysinfo/ssusysinfo.h>

static bool hearingAidSupported()
{
    ssusysinfo_t *sysinfo = ssusysinfo_create();
    if (!sysinfo)
        return false;

    const hw_feature_t feature = ssusysinfo_hw_feature_from_name("Feature_HAC");
    const bool supported = feature != Feature_Invalid
            && ssusysinfo_has_hw_feature(sysinfo, feature);
    ssusysinfo_delete(sysinfo);
    return supported;
}
#endif

Q_DECL_EXPORT int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    QCoreApplication::setOrganizationName("nemomobile");
    QCoreApplication::setApplicationName("voicecall");

    VoiceCallManager manager;
    BasicVoiceCallConfigurator configurator;

    if (!configurator.configure(&manager)) {
        qFatal("VoiceCall: configurator failed; exiting");
        return -1;
    }

    CellBroadcastDaemon cellBroadcastDaemon;
    if (!cellBroadcastDaemon.registerObject()) {
        qWarning("VoiceCall: failed to register cell broadcast D-Bus object");
    }

#ifdef HAVE_HEARING_AID_PREFERENCE
    if (hearingAidSupported())
        new HearingAidPreference(&app);
#endif

    return app.exec();
}
