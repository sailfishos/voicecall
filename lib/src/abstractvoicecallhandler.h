/*
 * This file is a part of the Voice Call Manager Plugin project.
 *
 * Copyright (C) 2011-2012  Tom Swindell <t.swindell@rubyx.co.uk>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */
#ifndef ABSTRACTVOICECALLHANDLER_H
#define ABSTRACTVOICECALLHANDLER_H

#include <QObject>
#include <QDateTime>

class AbstractVoiceCallProvider;

class AbstractVoiceCallHandler : public QObject
{
    Q_OBJECT

    Q_ENUMS(VoiceCallStatus)
    Q_ENUMS(VoiceCallFilterAction)

    Q_PROPERTY(QString handlerId READ handlerId CONSTANT)
    Q_PROPERTY(AbstractVoiceCallProvider* provider READ provider)
    Q_PROPERTY(VoiceCallStatus status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
    Q_PROPERTY(QString lineId READ lineId NOTIFY lineIdChanged)
    Q_PROPERTY(QDateTime startedAt READ startedAt NOTIFY startedAtChanged)
    Q_PROPERTY(int duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(bool isIncoming READ isIncoming CONSTANT)
    Q_PROPERTY(bool isEmergency READ isEmergency NOTIFY emergencyChanged)
    Q_PROPERTY(bool isMultiparty READ isMultiparty NOTIFY multipartyChanged)
    Q_PROPERTY(bool isForwarded READ isForwarded NOTIFY forwardedChanged)
    Q_PROPERTY(bool isRemoteHeld READ isRemoteHeld NOTIFY remoteHeldChanged)
    Q_PROPERTY(QString parentHandlerId READ parentHandlerId NOTIFY parentHandlerIdChanged)
    Q_PROPERTY(QList<AbstractVoiceCallHandler*> childCalls READ childCalls NOTIFY childCallsChanged)

public:
    enum VoiceCallStatus {
        STATUS_NULL,
        STATUS_ACTIVE,
        STATUS_HELD,
        STATUS_DIALING,
        STATUS_ALERTING,
        STATUS_INCOMING,
        STATUS_WAITING,
        STATUS_DISCONNECTED,
        STATUS_REJECTED,
        STATUS_IGNORED
    };

    enum VoiceCallFilterAction {
        ACTION_CONTINUE,
        ACTION_IGNORE,
        ACTION_REJECT
    };

    explicit AbstractVoiceCallHandler(QObject *parent = 0) : QObject(parent) {/* ... */}
    virtual ~AbstractVoiceCallHandler() {/* ... */}

    virtual AbstractVoiceCallProvider* provider() const = 0;

    virtual QString handlerId() const = 0;
    virtual QString lineId() const = 0;
    virtual QString subscriberId() const = 0;
    virtual QDateTime startedAt() const = 0;
    virtual int duration() const = 0;
    virtual bool isIncoming() const = 0;
    virtual bool isMultiparty() const = 0;
    virtual bool isEmergency() const = 0;
    virtual bool isForwarded() const = 0;
    virtual bool isRemoteHeld() const = 0;
    virtual QString parentHandlerId() const = 0;
    virtual QList<AbstractVoiceCallHandler*> childCalls() const = 0;

    virtual VoiceCallStatus status() const = 0;

    virtual bool isOngoing() const;
    QString statusText() const;

Q_SIGNALS:
    void statusChanged(VoiceCallStatus);
    void lineIdChanged(QString);
    void startedAtChanged(const QDateTime &);
    void durationChanged(int);
    void emergencyChanged(bool);
    void multipartyChanged(bool);
    void forwardedChanged(bool);
    void remoteHeldChanged(bool);
    void parentHandlerIdChanged(QString);
    void childCallsChanged();

public Q_SLOTS:
    virtual void answer() = 0;
    virtual void hangup() = 0;
    virtual void hold(bool on) = 0;
    virtual void deflect(const QString &target) = 0;
    virtual void sendDtmf(const QString &tones) = 0;
    virtual void merge(const QString &callHandle) = 0;
    virtual void split() = 0;
    virtual void filter(VoiceCallFilterAction action) = 0;
};

#endif // ABSTRACTVOICECALLHANDLER_H
