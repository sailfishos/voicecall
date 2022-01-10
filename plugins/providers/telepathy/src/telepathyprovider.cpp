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
#include "telepathyprovider.h"

#include "callchannelhandler.h"
#include "streamchannelhandler.h"

#include <TelepathyQt/CallChannel>
#include <TelepathyQt/StreamedMediaChannel>
#include <TelepathyQt/PendingReady>
#include <TelepathyQt/PendingChannel>
#include <TelepathyQt/PendingChannelRequest>

class TelepathyProviderPrivate
{
    Q_DECLARE_PUBLIC(TelepathyProvider)

public:
    TelepathyProviderPrivate(Tp::AccountPtr a, VoiceCallManagerInterface *m, TelepathyProvider *q)
        : q_ptr(q), manager(m), account(a),
          tpChannelRequest(NULL)
    { /* ... */ }

    TelepathyProvider           *q_ptr;
    VoiceCallManagerInterface   *manager;

    Tp::AccountPtr               account;

    QString                      errorString;

    QHash<QString,BaseChannelHandler*> voiceCalls;

    Tp::PendingChannelRequest *tpChannelRequest;

    bool shouldForceReconnect() const;
};

TelepathyProvider::TelepathyProvider(Tp::AccountPtr account, VoiceCallManagerInterface *manager, QObject *parent)
    : AbstractVoiceCallProvider(parent),
      d_ptr(new TelepathyProviderPrivate(account, manager, this))
{
    TRACE
    QObject::connect(account.data()->becomeReady(), SIGNAL(finished(Tp::PendingOperation*)), SLOT(onAccountBecomeReady(Tp::PendingOperation*)));
}

TelepathyProvider::~TelepathyProvider()
{
    TRACE
    delete d_ptr;
}

QString TelepathyProvider::errorString() const
{
    TRACE
    Q_D(const TelepathyProvider);
    return d->errorString;
}

QString TelepathyProvider::providerId() const
{
    TRACE
    Q_D(const TelepathyProvider);
    return d->account->objectPath();
}

QString TelepathyProvider::providerType() const
{
    TRACE
    Q_D(const TelepathyProvider);
    return d->account.data()->protocolName();
}

QList<AbstractVoiceCallHandler*> TelepathyProvider::voiceCalls() const
{
    TRACE
    Q_D(const TelepathyProvider);
    QList<AbstractVoiceCallHandler*> calls;
    foreach (BaseChannelHandler *handler, d->voiceCalls)
        calls << handler;

    return calls;
}

BaseChannelHandler *TelepathyProvider::voiceCall(const QString &handlerId) const
{
    TRACE
    Q_D(const TelepathyProvider);
    return d->voiceCalls.value(handlerId);
}

BaseChannelHandler *TelepathyProvider::voiceCall(Tp::ChannelPtr channel) const
{
    Q_D(const TelepathyProvider);

    foreach (BaseChannelHandler *handler, d->voiceCalls) {
        if (handler->channel()->objectPath() == channel->objectPath()) {
            return handler;
        }
    }

    return 0;
}

bool TelepathyProvider::dial(const QString &msisdn)
{
    TRACE
    Q_D(TelepathyProvider);
    if (d->tpChannelRequest) {
        d->errorString = "Can't initiate a call when one is pending!";
        WARNING_T("%s", qPrintable(d->errorString));
        emit this->error(d->errorString);
        return false;
    }

    if (d->account->protocolName() == "sip") {
        d->tpChannelRequest = d->account->ensureAudioCall(msisdn, QString(), QDateTime::currentDateTime(),
                                                          TP_QT_IFACE_CLIENT + ".voicecall");
    } else if (d->account->protocolName() == "tel") {
        d->tpChannelRequest = d->account->ensureStreamedMediaAudioCall(msisdn, QDateTime::currentDateTime(),
                                                                       TP_QT_IFACE_CLIENT + ".voicecall");
    } else {
        d->errorString = "Attempting to dial an unknown protocol";
        WARNING_T("%s", qPrintable(d->errorString));
        emit this->error(d->errorString);
        return false;
    }

    QObject::connect(d->tpChannelRequest,
                     SIGNAL(finished(Tp::PendingOperation*)),
                     SLOT(onPendingRequestFinished(Tp::PendingOperation*)));
    QObject::connect(d->tpChannelRequest,
                     SIGNAL(channelRequestCreated(Tp::ChannelRequestPtr)),
                     SLOT(onChannelRequestCreated(Tp::ChannelRequestPtr)));

    return true;
}

bool TelepathyProvider::createConference(Tp::ChannelPtr channel1, Tp::ChannelPtr channel2)
{
    TRACE
    Q_D(TelepathyProvider);
    if (d->tpChannelRequest) {
        d->errorString = "Can't initiate a call when one is pending!";
        WARNING_T("%s", qPrintable(d->errorString));
        emit this->error(d->errorString);
        return false;
    }

    if (d->account->protocolName() == "sip") {
        WARNING_T("Conference calls not supported for SIP protocol");
    } else if (d->account->protocolName() == "tel") {
        QList<Tp::ChannelPtr> channels;
        channels << channel1 << channel2;
        WARNING_T("Create conference call");
        d->tpChannelRequest = d->account->createConferenceStreamedMediaCall(channels, QStringList(), QDateTime::currentDateTime(), TP_QT_IFACE_CLIENT + ".voicecall");
    } else {
        d->errorString = "Attempting to create conference on an unknown protocol";
        WARNING_T("%s", qPrintable(d->errorString));
        emit this->error(d->errorString);
        return false;
    }

    QObject::connect(d->tpChannelRequest,
                     SIGNAL(finished(Tp::PendingOperation*)),
                     SLOT(onPendingRequestFinished(Tp::PendingOperation*)));
    QObject::connect(d->tpChannelRequest,
                     SIGNAL(channelRequestCreated(Tp::ChannelRequestPtr)),
                     SLOT(onChannelRequestCreated(Tp::ChannelRequestPtr)));

    return true;
}

void TelepathyProvider::updateConferenceHoldState()
{
    Q_D(TelepathyProvider);
    BaseChannelHandler *confHandler = conferenceHandler();
    if (confHandler) {
        AbstractVoiceCallHandler::VoiceCallStatus status = AbstractVoiceCallHandler::STATUS_NULL;
        for (auto it = d->voiceCalls.begin(); it != d->voiceCalls.end(); ++it) {
            BaseChannelHandler *callHandler = *it;
            if (callHandler != confHandler && callHandler->parentHandlerId() == confHandler->handlerId()) {
                if (status == AbstractVoiceCallHandler::STATUS_NULL)
                    status = callHandler->status();
                else if (status != callHandler->status())
                    return;
            }
        }

        if (status != AbstractVoiceCallHandler::STATUS_NULL) {
            StreamChannelHandler *streamHandler = qobject_cast<StreamChannelHandler*>(confHandler);
            if (streamHandler)
                streamHandler->getHoldState();
        }
    }
}

void TelepathyProvider::onAccountBecomeReady(Tp::PendingOperation *op)
{
    TRACE
    Q_D(TelepathyProvider);
    if (op->isError()) {
        WARNING_T("Operation failed: %s: %s", qPrintable(op->errorName()), qPrintable(op->errorMessage()));
        d->errorString = QString("Telepathy Operation Failed: %1 - %2").arg(op->errorName(), op->errorMessage());
        emit this->error(d->errorString);
        return;
    }

    DEBUG_T("Account %s became ready.", qPrintable(d->account.data()->uniqueIdentifier()));

    QObject::connect(d->account.data(), SIGNAL(stateChanged(bool)), SLOT(onAccountAvailabilityChanged()));
    QObject::connect(d->account.data(), SIGNAL(onlinenessChanged(bool)), SLOT(onAccountAvailabilityChanged()));
    QObject::connect(d->account.data(), SIGNAL(connectionStatusChanged(Tp::ConnectionStatus)), SLOT(onAccountAvailabilityChanged()));
    this->onAccountAvailabilityChanged();
}

void TelepathyProvider::onAccountAvailabilityChanged()
{
    TRACE
    Q_D(TelepathyProvider);

    if (d->account.data()->isEnabled() && d->account.data()->isOnline()
            && d->account.data()->connectionStatus() == Tp::ConnectionStatusConnected) {
        d->manager->appendProvider(this);
    } else {
        d->manager->removeProvider(this);

        if (d->shouldForceReconnect()) {
            WARNING_T("Forcing account %s back online immediately", qPrintable(d->account.data()->uniqueIdentifier()));
            d->account->setRequestedPresence(Tp::Presence::available());
        }
    }
}

void TelepathyProvider::createHandler(Tp::ChannelPtr ch, const QDateTime &userActionTime)
{
    TRACE
    Q_D(TelepathyProvider);
    BaseChannelHandler *handler = 0;

    DEBUG_T("\tProcessing channel: %s", qPrintable(ch->objectPath()));

    Tp::CallChannelPtr callChannel = Tp::CallChannelPtr::dynamicCast(ch);
    if (callChannel && !callChannel.isNull()) {
        DEBUG_T("Found CallChannel interface.");
        handler = new CallChannelHandler(d->manager->generateHandlerId(), callChannel, userActionTime, this);
    }

    Tp::StreamedMediaChannelPtr streamChannel = Tp::StreamedMediaChannelPtr::dynamicCast(ch);
    if (streamChannel && !streamChannel.isNull()) {
        DEBUG_T("Found StreamedMediaChannel interface.");
        handler = new StreamChannelHandler(d->manager->generateHandlerId(), streamChannel, userActionTime, this);

        connect(handler, &BaseChannelHandler::channelMerged, this, &TelepathyProvider::onChannelMerged);
        connect(handler, &BaseChannelHandler::channelRemoved, this, &TelepathyProvider::onChannelRemoved);
        connect(streamChannel.data(), &Tp::Channel::conferenceChannelMerged, this, &TelepathyProvider::onChannelMerged);
        connect(streamChannel.data(), &Tp::Channel::conferenceChannelRemoved,
                this, [this](const Tp::ChannelPtr &channel, const Tp::Channel::GroupMemberChangeDetails &) {
            onChannelRemoved(channel);
        });
    }

    if (!handler)
        return;

    d->voiceCalls.insert(handler->handlerId(), handler);

    QObject::connect(handler, SIGNAL(error(QString)), SIGNAL(error(QString)));
    QObject::connect(handler, SIGNAL(invalidated(QString,QString)), SLOT(onHandlerInvalidated(QString,QString)));

    emit this->voiceCallAdded(handler);
    emit this->voiceCallsChanged();
}

BaseChannelHandler *TelepathyProvider::conferenceHandler() const
{
    Q_D(const TelepathyProvider);
    for (auto it = d->voiceCalls.begin(); it != d->voiceCalls.end(); ++it) {
        if ((*it)->isMultiparty()) {
            return *it;
        }
    }

    return 0;
}

void TelepathyProvider::onPendingRequestFinished(Tp::PendingOperation *op)
{
    TRACE
    Q_D(TelepathyProvider);

    if (op != d->tpChannelRequest)
        return;

    if (op->isError()) {
        WARNING_T("Operation failed: %s: %s", qPrintable(op->errorName()), qPrintable(op->errorMessage()));
        d->errorString = QString("Telepathy Operation Failed: %1 - %2").arg(op->errorName(), op->errorMessage());
        emit this->error(d->errorString);
    }

    d->tpChannelRequest = NULL;
}

void TelepathyProvider::onChannelRequestCreated(const Tp::ChannelRequestPtr &request)
{
    TRACE
    // There is no need to watch for success; the channel will be delivered to the handler.
    // pendingRequestFinished (emitted after the request succeeds) will clean up the rest.
    connect(request.data(), SIGNAL(failed(QString,QString)),
            SLOT(onDialFailed(QString,QString)));
}

void TelepathyProvider::onDialFailed(const QString &errorName, const QString &errorMessage)
{
    TRACE
    Q_D(TelepathyProvider);

    WARNING_T("Operation failed: %s: %s", qPrintable(errorName), qPrintable(errorMessage));
    d->errorString = QString("Telepathy Operation Failed: %1 - %2").arg(errorName, errorMessage);
    emit this->error(d->errorString);

    // onPendingRequestFinished will clean up the request
}

void TelepathyProvider::onHandlerInvalidated(const QString &errorName, const QString &errorMessage)
{
    TRACE
    Q_D(TelepathyProvider);

    BaseChannelHandler *handler = qobject_cast<BaseChannelHandler*>(QObject::sender());
    d->voiceCalls.remove(handler->handlerId());

    emit this->voiceCallRemoved(handler->handlerId());
    emit this->voiceCallsChanged();

    handler->deleteLater();

    if (!errorName.isEmpty() || !errorMessage.isEmpty()) {
        WARNING_T("Handler invalidated: %s: %s", qPrintable(errorName), qPrintable(errorMessage));
        d->errorString = QString("Telepathy Handler Invalidated: %1 - %2").arg(errorName, errorMessage);
        emit this->error(d->errorString);
    }
}

void TelepathyProvider::onChannelMerged(Tp::ChannelPtr channel)
{
    TRACE
    BaseChannelHandler *confHandler = conferenceHandler();
    if (!confHandler) {
        WARNING_T("Channel merged, but no conference call exists");
        return;
    }

    BaseChannelHandler *callHandler = voiceCall(channel);
    if (!callHandler) {
        WARNING_T("No call handler exists for: %s", qPrintable(channel->objectPath()));
        return;
    }

    confHandler->addChildCall(callHandler);
}

void TelepathyProvider::onChannelRemoved(Tp::ChannelPtr channel)
{
    TRACE
    BaseChannelHandler *callHandler = voiceCall(channel);

    if (!callHandler) {
        WARNING_T("No call handler exists for: %s", qPrintable(channel->objectPath()));
        return;
    }

    BaseChannelHandler *confHandler = conferenceHandler();
    if (!confHandler) {
        WARNING_T("Channel removed, but no conference call exists");
        return;
    }

    confHandler->removeChildCall(callHandler);
}

bool TelepathyProviderPrivate::shouldForceReconnect() const
{
    return account->cmName() == "ring";
}
