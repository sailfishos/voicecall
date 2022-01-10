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
#ifndef STREAMCHANNELHANDLER_H
#define STREAMCHANNELHANDLER_H

#include "basechannelhandler.h"

#include <TelepathyQt/Channel>

class TelepathyProvider;

class StreamChannelHandler : public BaseChannelHandler
{
    Q_OBJECT

public:
    explicit StreamChannelHandler(const QString &id, Tp::StreamedMediaChannelPtr channel, const QDateTime &userActionTime, TelepathyProvider *provider = 0);
    ~StreamChannelHandler();

    /*** AbstractVoiceCallHandler Implementation ***/
    AbstractVoiceCallProvider* provider() const;
    QString handlerId() const;
    QString lineId() const;
    QDateTime startedAt() const;
    int duration() const;
    bool isIncoming() const;
    bool isMultiparty() const;
    bool isEmergency() const;
    bool isForwarded() const;
    bool isRemoteHeld() const;
    QString parentHandlerId() const override;
    QList<AbstractVoiceCallHandler*> childCalls() const override;

    VoiceCallStatus status() const;

    /*** BaseChannelHandler Implementation ***/
    Tp::ChannelPtr channel() const override;
    void setParentHandlerId(const QString &handler) override;

    /*** StreamChannelHandler Implementation ***/
    void getHoldState();

public Q_SLOTS:
    /*** AbstractVoiceCallHandler Implementation ***/
    void answer();
    void hangup();
    void hold(bool on);
    void deflect(const QString &target);
    void sendDtmf(const QString &tones);
    void merge(const QString &callHandle);
    void split();

protected Q_SLOTS:
    void onStatusChanged();

    // TODO: Remove when tp-ring updated to call channel interface.
    // StreamedMediaChannel Interface Handling
    void onStreamedMediaChannelReady(Tp::PendingOperation *op);
    void onStreamedMediaChannelInvalidated(Tp::DBusProxy*,const QString &errorName, const QString &errorMessage);

    void onStreamedMediaChannelStreamAdded(const Tp::StreamedMediaStreamPtr &stream);
    void onStreamedMediaChannelStreamRemoved(const Tp::StreamedMediaStreamPtr &stream);

    void onStreamedMediaChannelStreamStateChanged(const Tp::StreamedMediaStreamPtr &stream, Tp::MediaStreamState state);
    void onStreamedMediaChannelStreamError(const Tp::StreamedMediaStreamPtr &stream, Tp::MediaStreamError errorCode, const QString &errorMessage);

    void onStreamedMediaChannelAcceptCallFinished(Tp::PendingOperation *op);
    void onStreamedMediaChannelHangupCallFinished(Tp::PendingOperation *op);

    // StreamedMediaChannel CallState Interface Handling
    void onStreamedMediaChannelCallStateChanged(uint,uint);
    void onStreamedMediaChannelCallGetCallStatesFinished(QDBusPendingCallWatcher*);

    // StreamedMediaChannel Group Interface Handling
    void onStreamedMediaChannelGroupMembersChanged(QString message, Tp::UIntList added, Tp::UIntList removed, Tp::UIntList localPending, Tp::UIntList remotePending, uint actor, uint reason);

    // StreamedMediaChannel Hold Interface Handling
    void onStreamedMediaChannelHoldStateChanged(uint state, uint reason);

    void updateEmergencyStatus(const Tp::ServicePoint& servicePoint);

    // StreamedMediaChannel Confernce Interface Handling
    void onStreamedMediaChannelConferenceSplitChannelFinished(Tp::PendingOperation *op);
    void onStreamedMediaChannelConferenceMergeChannelFinished(Tp::PendingOperation *op);

protected:
    void timerEvent(QTimerEvent *event);
    void addChildCall(BaseChannelHandler *handler) override;
    void removeChildCall(BaseChannelHandler *handler) override;

private:
    void setStatus(VoiceCallStatus newStatus);

    class StreamChannelHandlerPrivate *d_ptr;

    Q_DISABLE_COPY(StreamChannelHandler)
    Q_DECLARE_PRIVATE(StreamChannelHandler)
};

#endif // STREAMCHANNELHANDLER_H
