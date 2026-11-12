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
#include "common.h"

#include "voicecallmanagerdbusservice.h"
#include "voicecallmanagerdbusadapter.h"
#include "voicecallhandlerdbusadapter.h"

#include <voicecallmanagerinterface.h>

#include <QDBusError>
#include <QDBusConnection>
#include <QDBusMetaType>
#include <QHash>
#include <QPointer>
#include <QSet>
#include <QTimer>

/*
 * How long a removed call stays on the bus when its provider keeps the
 * handler alive. Calls whose handler is deleted (the ofono provider) leave
 * as soon as it is; see onVoiceCallRemoved().
 */
static const int RemovedCallUnregisterDelay = 1000;

class VoiceCallManagerDBusServicePrivate
{
    Q_DECLARE_PUBLIC(VoiceCallManagerDBusService)

public:
    VoiceCallManagerDBusServicePrivate(VoiceCallManagerDBusService *q)
        : q_ptr(q), manager(NULL), managerAdapter(NULL)
    {/* ... */}

    VoiceCallManagerDBusService *q_ptr;

    VoiceCallManagerInterface *manager;
    VoiceCallManagerDBusAdapter *managerAdapter;

    // Handlers registered at /calls/<handlerId>, and those of them removed
    // from the manager but still registered.
    QHash<QString, QPointer<AbstractVoiceCallHandler> > registered;
    QSet<QString> removing;
};

VoiceCallManagerDBusService::VoiceCallManagerDBusService(QObject *parent)
    : AbstractVoiceCallManagerPlugin(parent), d_ptr(new VoiceCallManagerDBusServicePrivate(this))
{
    TRACE
    qDBusRegisterMetaType<AbstractVoiceCallHandler::VoiceCallFilterAction>();
}

VoiceCallManagerDBusService::~VoiceCallManagerDBusService()
{
    TRACE
    Q_D(VoiceCallManagerDBusService);
    delete d;
}

QString VoiceCallManagerDBusService::pluginId() const
{
    TRACE
    return "vcm-dbus-plugin";
}

bool VoiceCallManagerDBusService::initialize()
{
    TRACE
    return true;
}

bool VoiceCallManagerDBusService::configure(VoiceCallManagerInterface *manager)
{
    TRACE
    Q_D(VoiceCallManagerDBusService);

    d->manager = manager;
    d->managerAdapter = new VoiceCallManagerDBusAdapter(manager);

    if (!QDBusConnection::sessionBus().registerObject("/", manager)) {
        WARNING_T("Failed to register DBus object: %s", qPrintable(QDBusConnection::sessionBus().lastError().message()));
        return false;
    }

    if (!QDBusConnection::sessionBus().registerService("org.nemomobile.voicecall")) {
        WARNING_T("Failed to register DBus service: %s", qPrintable(QDBusConnection::sessionBus().lastError().message()));
        return false;
    }

    QObject::connect(manager, SIGNAL(voiceCallAdded(AbstractVoiceCallHandler*)), SLOT(onVoiceCallAdded(AbstractVoiceCallHandler*)));
    QObject::connect(manager, SIGNAL(voiceCallRemoved(QString)), SLOT(onVoiceCallRemoved(QString)));
    QObject::connect(manager, SIGNAL(activeVoiceCallChanged()), SLOT(onActiveVoiceCallChanged()));

    d->managerAdapter->configure(manager);
    return true;
}

bool VoiceCallManagerDBusService::start()
{
    TRACE
    return true;
}

bool VoiceCallManagerDBusService::suspend()
{
    TRACE
    return true;
}

bool VoiceCallManagerDBusService::resume()
{
    TRACE
    return true;
}

void VoiceCallManagerDBusService::finalize()
{
    TRACE
}

void VoiceCallManagerDBusService::onVoiceCallAdded(AbstractVoiceCallHandler *handler)
{
    TRACE
    Q_D(VoiceCallManagerDBusService);

    // A provider that goes away and comes back (a telepathy account going
    // offline and online again) announces its surviving calls again. Adapt
    // each handler once, or every D-Bus signal of the call is sent twice.
    if (!handler->findChild<VoiceCallHandlerDBusAdapter *>(QString(), Qt::FindDirectChildrenOnly))
        new VoiceCallHandlerDBusAdapter(handler);

    const QString handlerId = handler->handlerId();
    d->removing.remove(handlerId);
    if (d->registered.value(handlerId) == handler)
        return;     // back before its removal took effect, still registered

    if (!QDBusConnection::sessionBus().registerObject("/calls/" + handlerId, handler)) {
        WARNING_T("Failed to register DBus object: %s", qPrintable(QDBusConnection::sessionBus().lastError().message()));
        return;
    }
    d->registered.insert(handlerId, handler);
}

void VoiceCallManagerDBusService::onVoiceCallRemoved(const QString &handlerId)
{
    TRACE
    Q_D(VoiceCallManagerDBusService);

    /*
     * Not unregisterObject() right away. QtDBus relays an adaptor's signal
     * through a queued call to its own thread, which only then looks up the
     * paths the object is registered at; unregistered by then, the signal is
     * dropped. A call that ends announces its last status ("disconnected")
     * and is removed straight after, so its last statusChanged was often
     * lost. When the handler is deleted, QtDBus unregisters it itself, through
     * the same queue and after the signals already in it, so wait for that.
     * Providers that keep the handler alive get it unregistered a little
     * later instead.
     */
    QPointer<AbstractVoiceCallHandler> handler = d->registered.value(handlerId);
    if (!handler) {
        d->registered.remove(handlerId);
        return;
    }
    d->removing.insert(handlerId);
    QTimer::singleShot(RemovedCallUnregisterDelay, this, [this, handlerId]() {
        Q_D(VoiceCallManagerDBusService);
        if (!d->removing.remove(handlerId))
            return;     // added again in the meantime
        if (d->registered.take(handlerId))
            QDBusConnection::sessionBus().unregisterObject("/calls/" + handlerId);
    });
}

void VoiceCallManagerDBusService::onActiveVoiceCallChanged()
{
    TRACE
    Q_D(VoiceCallManagerDBusService);

    // registerObject() refuses a path that is still registered, and the
    // active call can change from one call to another.
    QDBusConnection::sessionBus().unregisterObject("/calls/active");
    if (d->manager->activeVoiceCall()) {
        DEBUG_T("VoiceCallManagerDBusService:: registering active voice call interface.");
        QDBusConnection::sessionBus().registerObject("/calls/active", d->manager->activeVoiceCall());
    }
}
