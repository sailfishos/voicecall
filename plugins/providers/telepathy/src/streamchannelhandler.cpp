/*
 * This file is a part of the Voice Call Manager project
 *
 * Copyright (C) 2011-2015  Tom Swindell <tom.swindell@jollamobile.com>
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
#include "streamchannelhandler.h"

#include "telepathyprovider.h"

#include <TelepathyQt/Channel>

#include <TelepathyQt/PendingReady>
#include <TelepathyQt/PendingChannel>
#include <TelepathyQt/PendingVariant>

#include <TelepathyQt/StreamedMediaChannel>

#include <QElapsedTimer>
#include <qmath.h>

static quint64 get_tick()
{
#if defined(CLOCK_BOOTTIME)
    int id = CLOCK_BOOTTIME;
#else
    int id = CLOCK_MONOTONIC;
#endif

    quint64 res = 0;

    struct timespec ts;

    if (clock_gettime(id, &ts) == 0) {
        res = ts.tv_sec;
        res *= 1000;
        res += ts.tv_nsec / 1000000;
    }

    return res;
}

class StreamChannelHandlerPrivate
{
    Q_DECLARE_PUBLIC(StreamChannelHandler)

public:
    StreamChannelHandlerPrivate(StreamChannelHandler *q, const QString &id, Tp::StreamedMediaChannelPtr c, const QDateTime &s, TelepathyProvider *p)
        : q_ptr(q), pendingHangup(NULL), handlerId(id), provider(p), startedAt(s), status(AbstractVoiceCallHandler::STATUS_NULL),
          channel(c), servicePointInterface(NULL), duration(0), durationTimerId(-1), isEmergency(false),
          isForwarded(false), isIncoming(false), isRemoteHeld(false)
    { /* ... */ }

    void listenToEmergencyStatus()
    {
        TRACE
        if (channel && channel->isReady() && !servicePointInterface) {
            servicePointInterface = channel->optionalInterface<Tp::Client::ChannelInterfaceServicePointInterface>();
            if (servicePointInterface) {
                // listen to changes in emergency call state, dictated by service point type
                q_ptr->connect(servicePointInterface, SIGNAL(ServicePointChanged(const Tp::ServicePoint &)),
                        q_ptr, SLOT(updateEmergencyStatus(const Tp::ServicePoint &)));

                // fetch initial emergency call status
                QString initialServicePointProperty = TP_QT_IFACE_CHANNEL_INTERFACE_SERVICE_POINT+QLatin1String(".InitialServicePoint");
                QVariant servicePointProperty = channel->immutableProperties().value(initialServicePointProperty);
                if (servicePointProperty.isValid()) {
                    const Tp::ServicePoint servicePoint = qdbus_cast<Tp::ServicePoint>(servicePointProperty);
                    q_ptr->updateEmergencyStatus(servicePoint);
                } else {
                    const Tp::ServicePoint servicePoint = servicePointInterface->property("CurrentServicePoint").value<Tp::ServicePoint>();
                    q_ptr->updateEmergencyStatus(servicePoint);
                }
            }
        }
    }

    StreamChannelHandler  *q_ptr;
    QPointer<Tp::PendingOperation>  pendingHangup;

    QString            handlerId;
    QString            parentHandlerId;
    TelepathyProvider *provider;

    QList<AbstractVoiceCallHandler*> childCalls;

    QDateTime          startedAt;

    AbstractVoiceCallHandler::VoiceCallStatus status;

    Tp::StreamedMediaChannelPtr channel;
    Tp::Client::ChannelInterfaceServicePointInterface *servicePointInterface;

    quint64 duration;
    quint64 connectedAt;
    int durationTimerId;
    QElapsedTimer elapsedTimer;
    bool isEmergency;
    bool isForwarded;
    bool isIncoming;
    bool isRemoteHeld;
};

StreamChannelHandler::StreamChannelHandler(const QString &id, Tp::StreamedMediaChannelPtr channel, const QDateTime &userActionTime, TelepathyProvider *provider)
    : BaseChannelHandler(provider), d_ptr(new StreamChannelHandlerPrivate(this, id, channel, userActionTime, provider))
{
    TRACE
    Q_D(StreamChannelHandler);

    QObject::connect(this, SIGNAL(statusChanged(VoiceCallStatus)), SLOT(onStatusChanged()));

    QObject::connect(d->channel->becomeReady(),
                     SIGNAL(finished(Tp::PendingOperation*)),
                     SLOT(onStreamedMediaChannelReady(Tp::PendingOperation*)));

    QObject::connect(d->channel.data(),
                     SIGNAL(invalidated(Tp::DBusProxy*,QString,QString)),
                     SLOT(onStreamedMediaChannelInvalidated(Tp::DBusProxy*,QString,QString)));

    d->listenToEmergencyStatus();
    emit this->startedAtChanged(startedAt());
}

StreamChannelHandler::~StreamChannelHandler()
{
    TRACE
    Q_D(StreamChannelHandler);
    if (!d->parentHandlerId.isEmpty()) {
        if (BaseChannelHandler *confHandler = d->provider->voiceCall(d->parentHandlerId))
            confHandler->removeChildCall(this);
    }

    foreach (AbstractVoiceCallHandler *callHandler, d->childCalls) {
        static_cast<BaseChannelHandler*>(callHandler)->setParentHandlerId(QString());
    }

    delete this->d_ptr;
}

AbstractVoiceCallProvider* StreamChannelHandler::provider() const
{
    Q_D(const StreamChannelHandler);
    return d->provider;
}

QString StreamChannelHandler::handlerId() const
{
    Q_D(const StreamChannelHandler);
    return d->handlerId;
}

Tp::ChannelPtr StreamChannelHandler::channel() const
{
    Q_D(const StreamChannelHandler);
    return d->channel;
}

void StreamChannelHandler::setParentHandlerId(const QString &handler)
{
    TRACE
    Q_D(StreamChannelHandler);
    if (handler != d->parentHandlerId) {
        d->parentHandlerId = handler;
        emit parentHandlerIdChanged(handler);
    }
}

QString StreamChannelHandler::lineId() const
{
    Q_D(const StreamChannelHandler);
    if (!d->channel->isReady())
        return QString();
    return d->channel->targetId();
}

QDateTime StreamChannelHandler::startedAt() const
{
    Q_D(const StreamChannelHandler);
    return d->startedAt;
}

int StreamChannelHandler::duration() const
{
    Q_D(const StreamChannelHandler);
    return int(qRound(d->duration/1000.0));
}

bool StreamChannelHandler::isIncoming() const
{
    Q_D(const StreamChannelHandler);
    return d->isIncoming;
}

bool StreamChannelHandler::isMultiparty() const
{
    Q_D(const StreamChannelHandler);
    if (!d->channel->isReady())
        return false;
    return d->channel->isConference();
}

bool StreamChannelHandler::isEmergency() const
{
    TRACE
    Q_D(const StreamChannelHandler);
    if (!d->channel->isReady())
        return false;
    return d->isEmergency;
}

bool StreamChannelHandler::isForwarded() const
{
    Q_D(const StreamChannelHandler);
    if (!d->channel->isReady())
        return false;
    return d->isForwarded;
}

bool StreamChannelHandler::isRemoteHeld() const
{
    Q_D(const StreamChannelHandler);
    if (!d->channel->isReady())
        return false;
    return d->isRemoteHeld;
}

QString StreamChannelHandler::parentHandlerId() const
{
    Q_D(const StreamChannelHandler);
    return d->parentHandlerId;
}

QList<AbstractVoiceCallHandler*> StreamChannelHandler::childCalls() const
{
    Q_D(const StreamChannelHandler);
    return d->childCalls;
}

AbstractVoiceCallHandler::VoiceCallStatus StreamChannelHandler::status() const
{
    Q_D(const StreamChannelHandler);
    return d->status;
}

void StreamChannelHandler::answer()
{
    TRACE
    Q_D(StreamChannelHandler);

    QObject::connect(d->channel.data()->acceptCall(),
                     SIGNAL(finished(Tp::PendingOperation*)),
                     SLOT(onStreamedMediaChannelAcceptCallFinished(Tp::PendingOperation*)));

    setStatus(STATUS_ACTIVE);
}

void StreamChannelHandler::hangup()
{
    TRACE
    Q_D(StreamChannelHandler);

    if (d->pendingHangup) {
        if (d->pendingHangup->isFinished()) {
            d->pendingHangup = NULL;
        } else {
            DEBUG_T("Filtering out hangup request, earlier request still pending");
        }
    }

    if (!d->pendingHangup) {
        d->pendingHangup = d->channel.data()->hangupCall();
        QObject::connect(d->pendingHangup,
                         SIGNAL(finished(Tp::PendingOperation*)),
                         SLOT(onStreamedMediaChannelHangupCallFinished(Tp::PendingOperation*)));
    }
}

void StreamChannelHandler::hold(bool on)
{
    TRACE
    Q_D(StreamChannelHandler);
    Tp::Client::ChannelInterfaceHoldInterface *holdIface = new Tp::Client::ChannelInterfaceHoldInterface(d->channel.data(), this);
    holdIface->RequestHold(on);
}

void StreamChannelHandler::getHoldState()
{
    TRACE
    Q_D(StreamChannelHandler);
    Tp::Client::ChannelInterfaceHoldInterface *holdIface = new Tp::Client::ChannelInterfaceHoldInterface(d->channel.data(), this);
    QDBusPendingReply<uint, uint> reply = holdIface->GetHoldState();
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(reply, this);
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished,
                     this, [this](QDBusPendingCallWatcher *watcher) {
        QDBusPendingReply<uint, uint> reply = *watcher;
        if (!reply.isError()) {
            onStreamedMediaChannelHoldStateChanged(reply.argumentAt(0).toUInt(), reply.argumentAt(1).toUInt());
        }
        watcher->deleteLater();
    });
}

//FIXME: Don't know what telepathy API provides this.
void StreamChannelHandler::deflect(const QString &target)
{
    TRACE
    Q_UNUSED(target)
    emit this->error("NOT IMPLEMENTED YET!");
}

void StreamChannelHandler::sendDtmf(const QString &tones)
{
    TRACE
    Q_D(StreamChannelHandler);
    Tp::Client::ChannelInterfaceDTMFInterface *dtmfIface = new Tp::Client::ChannelInterfaceDTMFInterface(d->channel.data(), this);

    bool ok = true;
    unsigned int toneId = tones.toInt(&ok);

    if (!ok) {
        if (tones == "*") toneId = 10;
        else if (tones == "#") toneId = 11;
        else if (tones == "A") toneId = 12;
        else if (tones == "B") toneId = 13;
        else if (tones == "C") toneId = 14;
        else if (tones == "D") toneId = 15;
        else return;
    }

    dtmfIface->StartTone(1, toneId, 0);
    //dtmfIface->MultipleTones(tones);
}

void StreamChannelHandler::split()
{
    Q_D(StreamChannelHandler);
    QObject::connect(d->channel->conferenceSplitChannel(),
                     SIGNAL(finished(Tp::PendingOperation*)),
                     SLOT(onStreamedMediaChannelConferenceSplitChannelFinished(Tp::PendingOperation*)));
}

void StreamChannelHandler::merge(const QString &callHandle)
{
    TRACE
    Q_D(StreamChannelHandler);

    StreamChannelHandler *handler = qobject_cast<StreamChannelHandler*>(d->provider->voiceCall(callHandle));

    if (!handler) {
        WARNING_T("Cannot merge call: %s", qPrintable(callHandle));
        return;
    }

    if (isMultiparty()) {
        DEBUG_T("Merge %s into existing conference call", qPrintable(callHandle));
        QObject::connect(d->channel->conferenceMergeChannel(handler->channel()),
                         SIGNAL(finished(Tp::PendingOperation*)),
                         SLOT(onStreamedMediaChannelConferenceMergeChannelFinished(Tp::PendingOperation*)));
    } else {
        DEBUG_T("Create a new conference call");
        d->provider->createConference(d->channel, handler->channel());
    }
}

void StreamChannelHandler::updateEmergencyStatus(const Tp::ServicePoint& servicePoint)
{
    TRACE
    Q_D(StreamChannelHandler);
    bool isEmergency = (servicePoint.servicePointType == Tp::ServicePointTypeEmergency);

    if (d->isEmergency != isEmergency) {
        d->isEmergency = isEmergency;
        emit emergencyChanged(d->isEmergency);
    }
}

void StreamChannelHandler::onStreamedMediaChannelReady(Tp::PendingOperation *op)
{
    TRACE
    Q_D(StreamChannelHandler);
    if (op->isError()) {
        WARNING_T("Operation failed: %s: %s", qPrintable(op->errorName()), qPrintable(op->errorMessage()));
        emit this->error(QString("Telepathy Operation Failed: %1 - %2").arg(op->errorName(), op->errorMessage()));
        return;
    }

    DEBUG_T("StreamedMediaChannel Ready:");
    qDebug() << "\tType:" << d->channel->channelType();
    qDebug() << "\tInterfaces:" << d->channel->interfaces();

    QObject::connect(d->channel.data(),
                     SIGNAL(streamAdded(Tp::StreamedMediaStreamPtr)),
                     SLOT(onStreamedMediaChannelStreamAdded(Tp::StreamedMediaStreamPtr)));

    QObject::connect(d->channel.data(),
                     SIGNAL(streamRemoved(Tp::StreamedMediaStreamPtr)),
                     SLOT(onStreamedMediaChannelStreamRemoved(Tp::StreamedMediaStreamPtr)));

    QObject::connect(d->channel.data(),
                     SIGNAL(streamError(Tp::StreamedMediaStreamPtr,Tp::MediaStreamError,QString)),
                     SLOT(onStreamedMediaChannelStreamError(Tp::StreamedMediaStreamPtr,Tp::MediaStreamError,QString)));

    QObject::connect(d->channel.data(),
                     SIGNAL(streamStateChanged(Tp::StreamedMediaStreamPtr,Tp::MediaStreamState)),
                     SLOT(onStreamedMediaChannelStreamStateChanged(Tp::StreamedMediaStreamPtr,Tp::MediaStreamState)));

    if (d->channel->hasInterface(TP_QT_IFACE_CHANNEL_INTERFACE_CALL_STATE)) {
        DEBUG_T("Creating CallState interface");
        Tp::Client::ChannelInterfaceCallStateInterface *csIface = new Tp::Client::ChannelInterfaceCallStateInterface(d->channel.data(), this);
        QDBusPendingReply<Tp::ChannelCallStateMap> reply = csIface->GetCallStates();
        QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(reply, this);
        QObject::connect(watcher, SIGNAL(finished(QDBusPendingCallWatcher*)),
                         SLOT(onStreamedMediaChannelCallGetCallStatesFinished(QDBusPendingCallWatcher*)));
        QObject::connect(csIface,
                         SIGNAL(CallStateChanged(uint,uint)),
                         SLOT(onStreamedMediaChannelCallStateChanged(uint,uint)));
    }

    if (d->channel->hasInterface(TP_QT_IFACE_CHANNEL_INTERFACE_GROUP)) {
        DEBUG_T("Creating Group interface");
        Tp::Client::ChannelInterfaceGroupInterface *groupIface = new Tp::Client::ChannelInterfaceGroupInterface(d->channel.data(), this);
        QObject::connect(groupIface, SIGNAL(MembersChanged(QString,Tp::UIntList,Tp::UIntList,Tp::UIntList,Tp::UIntList,uint,uint)),
                         SLOT(onStreamedMediaChannelGroupMembersChanged(QString,Tp::UIntList,Tp::UIntList,Tp::UIntList,Tp::UIntList,uint,uint)));
    }

    if (d->channel->hasInterface(TP_QT_IFACE_CHANNEL_INTERFACE_HOLD)) {
        DEBUG_T("Creating Hold interface");
        Tp::Client::ChannelInterfaceHoldInterface *holdIface = new Tp::Client::ChannelInterfaceHoldInterface(d->channel.data(), this);
        getHoldState();
        QObject::connect(holdIface,
                         SIGNAL(HoldStateChanged(uint,uint)),
                         SLOT(onStreamedMediaChannelHoldStateChanged(uint,uint)));
    }

    if (d->channel->hasInterface(TP_QT_IFACE_CHANNEL_INTERFACE_CONFERENCE)) {
        DEBUG_T("Creating Conference interface");
        QList<Tp::ChannelPtr> channels = d->channel->conferenceChannels();
        foreach (Tp::ChannelPtr channel, channels)
            emit channelMerged(channel);
    }

    d->listenToEmergencyStatus();

    emit lineIdChanged(lineId());
    emit multipartyChanged(isMultiparty());
    emit emergencyChanged(isEmergency());
    emit forwardedChanged(isForwarded());

    if (isMultiparty()) {
        setStatus(STATUS_ACTIVE);
    } else if (d->channel->isRequested()) {
        setStatus(STATUS_DIALING);
    } else {
        setStatus(STATUS_INCOMING);
    }

    d->isIncoming = !d->channel->isRequested();
}

void StreamChannelHandler::onStreamedMediaChannelInvalidated(Tp::DBusProxy *, const QString &errorName, const QString &errorMessage)
{
    TRACE
    Q_D(StreamChannelHandler);
    DEBUG_T("Channel invalidated: %s: %s", qPrintable(errorName), qPrintable(errorMessage));

    // It seems to get called twice.
    QObject::disconnect(d->channel.data(),
                        SIGNAL(invalidated(Tp::DBusProxy*,QString,QString)),
                        this,
                        SLOT(onStreamedMediaChannelInvalidated(Tp::DBusProxy*,QString,QString)));

    if (d->status == STATUS_DISCONNECTED && !d->childCalls.isEmpty()) {
        // Conference call is in disconnect state and still has children.
        // Keep the conference handler alive in the disconnected state
        // until all children have been split or destroyed. This ensures that
        // the UI can display a disconnected conference call until all connections are
        // removed, rather than seeing individual calls being disconnected one at a time.
        return;
    }

    foreach (AbstractVoiceCallHandler *callHandler, d->childCalls) {
        static_cast<BaseChannelHandler*>(callHandler)->setParentHandlerId(QString());
    }

    setStatus(STATUS_NULL);
    emit this->invalidated(errorName, errorMessage);
}

void StreamChannelHandler::onStreamedMediaChannelStreamAdded(const Tp::StreamedMediaStreamPtr &stream)
{
    TRACE
    Q_UNUSED(stream)
}

void StreamChannelHandler::onStreamedMediaChannelStreamRemoved(const Tp::StreamedMediaStreamPtr &stream)
{
    TRACE
    Q_UNUSED(stream)
}

void StreamChannelHandler::onStreamedMediaChannelStreamError(const Tp::StreamedMediaStreamPtr &stream, Tp::MediaStreamError errorCode, const QString &errorMessage)
{
    TRACE
    Q_UNUSED(stream)
    Q_UNUSED(errorCode)

    emit this->error(QString("Telepathy Stream Error: %1").arg(errorMessage));
}

void StreamChannelHandler::onStreamedMediaChannelStreamStateChanged(const Tp::StreamedMediaStreamPtr &stream, Tp::MediaStreamState state)
{
    TRACE
    Q_UNUSED(stream)

    switch(state)
    {
    case Tp::MediaStreamStateDisconnected:
        DEBUG_T("Media stream state disconnected.");
        setStatus(STATUS_DISCONNECTED);
        break;

    case Tp::MediaStreamStateConnecting:
        DEBUG_T("Media stream state connecting.");
        break;

    case Tp::MediaStreamStateConnected:
        DEBUG_T("Media stream state connected.");
        setStatus(STATUS_ALERTING);
        break;

    default:
        break;
    }
}

void StreamChannelHandler::onStreamedMediaChannelAcceptCallFinished(Tp::PendingOperation *op)
{
    TRACE
    if (op->isError()) {
        WARNING_T("Operation failed: %s: %s", qPrintable(op->errorName()), qPrintable(op->errorMessage()));
        emit this->error(QString("Telepathy Operation Failed: %1 - %2").arg(op->errorName(), op->errorMessage()));
        this->hangup();
        return;
    }

    setStatus(STATUS_ACTIVE);
}

void StreamChannelHandler::onStreamedMediaChannelHangupCallFinished(Tp::PendingOperation *op)
{
    TRACE
    Q_D(StreamChannelHandler);
    d->pendingHangup = NULL;

    if (op->isError()) {
        WARNING_T("Operation failed: %s: %s", qPrintable(op->errorName()), qPrintable(op->errorMessage()));
        emit this->error(QString("Telepathy Operation Failed: %1 - %2").arg(op->errorName(), op->errorMessage()));
        this->hangup();
        return;
    }

    setStatus(STATUS_DISCONNECTED);
}

void StreamChannelHandler::onStreamedMediaChannelCallStateChanged(uint, uint state)
{
    TRACE
    Q_D(StreamChannelHandler);

    bool forwarded = state & Tp::ChannelCallStateForwarded;
    bool held = state & Tp::ChannelCallStateHeld;

    if ((d->status == STATUS_HELD) && !held) {
        setStatus(STATUS_ACTIVE);
        d->isRemoteHeld = false;
        emit remoteHeldChanged(d->isRemoteHeld);
    }

    if (d->status != STATUS_HELD && held) {
        setStatus(STATUS_HELD);
        d->isRemoteHeld = true;
        emit remoteHeldChanged(d->isRemoteHeld);
    }

    if (forwarded != d->isForwarded) {
        d->isForwarded = forwarded;
        DEBUG_T("Call forwarded: %s", forwarded ? "true" : "false");
        emit forwardedChanged(d->isForwarded);
    }
}

void StreamChannelHandler::onStreamedMediaChannelCallGetCallStatesFinished(QDBusPendingCallWatcher *call)
{
    TRACE
    QDBusPendingReply<Tp::ChannelCallStateMap> reply = *call;
    if (!reply.isError()) {
        QMap<uint, uint> states = reply.value();
        for (QMap<uint, uint>::Iterator it = states.begin(); it != states.end(); ++it) {
            onStreamedMediaChannelCallStateChanged(it.key(), it.value());
        }
    }
    call->deleteLater();
}

void StreamChannelHandler::onStreamedMediaChannelConferenceSplitChannelFinished(Tp::PendingOperation *op)
{
    TRACE
    Q_D(StreamChannelHandler);
    if (op->isError()) {
        WARNING_T("Operation failed: %s: %s", qPrintable(op->errorName()), qPrintable(op->errorMessage()));
        emit this->error(QString("Telepathy Operation Failed: %1 - %2").arg(op->errorName(), op->errorMessage()));
        return;
    }

    emit channelRemoved(d->channel);
}

void StreamChannelHandler::onStreamedMediaChannelConferenceMergeChannelFinished(Tp::PendingOperation *op)
{
    if (op->isError()) {
        WARNING_T("Operation failed: %s: %s", qPrintable(op->errorName()), qPrintable(op->errorMessage()));
        emit this->error(QString("Telepathy Operation Failed: %1 - %2").arg(op->errorName(), op->errorMessage()));
        return;
    }

    // No need to do anything. We'll get a conferenceChannelMerged signal from the channel.
}

void StreamChannelHandler::onStreamedMediaChannelGroupMembersChanged(QString message, Tp::UIntList added, Tp::UIntList removed, Tp::UIntList localPending, Tp::UIntList remotePending, uint actor, uint reason)
{
    Q_UNUSED(message)
    Q_UNUSED(added)
    Q_UNUSED(removed)
    Q_UNUSED(localPending)
    Q_UNUSED(remotePending)
    Q_UNUSED(actor)
    Q_UNUSED(reason)
    TRACE
    Q_D(StreamChannelHandler);

    Tp::Client::ChannelInterfaceGroupInterface *groupIface = new Tp::Client::ChannelInterfaceGroupInterface(d->channel.data(), this);

    QDBusPendingReply<Tp::UIntList> reply = groupIface->GetMembers();
    reply.waitForFinished();

    if (reply.isValid()) {
        if (reply.value().count() == 0) {
            setStatus(STATUS_DISCONNECTED);
        } else if (d->status != STATUS_HELD) {
            setStatus(STATUS_ACTIVE);
        }
    }
}

void StreamChannelHandler::onStreamedMediaChannelHoldStateChanged(uint state, uint reason)
{
    TRACE
    Q_UNUSED(reason)
    Q_D(StreamChannelHandler);

    switch(state)
    {
    case Tp::LocalHoldStateUnheld:
        DEBUG_T("Hold state unheld: %s", qPrintable(lineId()));
        if (status() == STATUS_HELD)
            setStatus(STATUS_ACTIVE);
        break;
    case Tp::LocalHoldStateHeld:
        DEBUG_T("Hold state held: %s", qPrintable(lineId()));
        if (status() == STATUS_ACTIVE)
            setStatus(STATUS_HELD);
        break;
    }

    if (!d->parentHandlerId.isEmpty()) {
        // Force the conference call to get its hold state; it doesn't keep in sync with its children.
        // tp-ring should really take responsibility for this.
        d->provider->updateConferenceHoldState();
    }
}

void StreamChannelHandler::timerEvent(QTimerEvent *event)
{
    Q_D(StreamChannelHandler);

    if (isOngoing() && event->timerId() == d->durationTimerId) {
        d->duration = get_tick() - d->connectedAt;
        emit this->durationChanged(duration());
    }
}

void StreamChannelHandler::addChildCall(BaseChannelHandler *handler)
{
    TRACE
    Q_D(StreamChannelHandler);
    if (d->childCalls.contains(handler))
        return;
    DEBUG_T("Added child call: %s", qPrintable(handler->handlerId()));
    d->childCalls.append(handler);
    handler->setParentHandlerId(d->handlerId);
    emit childCallsChanged();
}

void StreamChannelHandler::removeChildCall(BaseChannelHandler *handler)
{
    TRACE
    Q_D(StreamChannelHandler);
    DEBUG_T("Removed child call: %s", qPrintable(handler->handlerId()));
    d->childCalls.removeAll(handler);
    handler->setParentHandlerId(QString());

    emit childCallsChanged();

    if (!d->channel->isValid() && d->childCalls.isEmpty()) {
        // Now that all of our children have been destroyed we'll allow ourself to be destroyed.
        emit invalidated(QString(), QString());
    }
}

void StreamChannelHandler::onStatusChanged()
{
    TRACE
    Q_D(StreamChannelHandler);

    if (isOngoing()) {
        if (d->durationTimerId == -1) {
            d->durationTimerId = this->startTimer(1000);
            d->elapsedTimer.start();
            d->connectedAt = get_tick();
        }
    } else if (d->durationTimerId != -1) {
        this->killTimer(d->durationTimerId);
        d->durationTimerId = -1;
    }
}

void StreamChannelHandler::setStatus(VoiceCallStatus newStatus)
{
    TRACE
    Q_D(StreamChannelHandler);
    if (newStatus == d->status)
        return;

    d->status = newStatus;
    emit statusChanged(d->status);

    if (d->status == STATUS_DISCONNECTED && !d->parentHandlerId.isEmpty()) {
        BaseChannelHandler *confHandler = d->provider->voiceCall(d->parentHandlerId);
        if (confHandler && confHandler->status() == STATUS_DISCONNECTED) {
            // Destroy this call immediately since the conference call is managing the disconnection
            // and we don't want the hangup to happen in multiple stages.
            emit invalidated(QString(), QString());
        }
    }
}
