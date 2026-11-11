/*
 * This file is a part of the Voice Call Manager project
 *
 * Copyright (C) 2026 Herman van Hazendonk <github.com@herrie.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef FAKE_VOICECALL_H
#define FAKE_VOICECALL_H

/*
 * In-memory provider and call handler for tests of the manager and its
 * plugins. The test drives them directly: addCall()/removeCall() emit what a
 * real provider emits, setStatus() what a real call emits, and the handler
 * records the requests made of it.
 */

#include <abstractvoicecallhandler.h>
#include <abstractvoicecallprovider.h>

#include <QList>
#include <QStringList>

class FakeVoiceCallHandler : public AbstractVoiceCallHandler
{
    Q_OBJECT

public:
    FakeVoiceCallHandler(const QString &id, AbstractVoiceCallProvider *provider,
                         bool incoming = false, VoiceCallStatus status = STATUS_NULL)
        : AbstractVoiceCallHandler(provider)
        , m_id(id)
        , m_provider(provider)
        , m_incoming(incoming)
        , m_status(status)
    {
    }

    AbstractVoiceCallProvider *provider() const override { return m_provider; }
    QString handlerId() const override { return m_id; }
    QString lineId() const override { return m_lineId; }
    QString subscriberId() const override { return QString(); }
    QDateTime startedAt() const override { return QDateTime(); }
    int duration() const override { return m_duration; }
    bool isIncoming() const override { return m_incoming; }
    bool isMultiparty() const override { return false; }
    bool isEmergency() const override { return m_emergency; }
    bool isForwarded() const override { return false; }
    bool isRemoteHeld() const override { return false; }
    QString parentHandlerId() const override { return QString(); }
    QList<AbstractVoiceCallHandler *> childCalls() const override
    {
        return QList<AbstractVoiceCallHandler *>();
    }
    VoiceCallStatus status() const override { return m_status; }

    void setStatus(VoiceCallStatus status)
    {
        m_status = status;
        emit statusChanged(status);
    }
    void setDuration(int seconds) { m_duration = seconds; }
    void setEmergency(bool emergency) { m_emergency = emergency; }
    void setLineId(const QString &lineId) { m_lineId = lineId; }

    // Requests made of this call, e.g. "hangup", "filter:0", "dtmf:1"
    QStringList requests;

public Q_SLOTS:
    void answer() override { requests << QStringLiteral("answer"); }
    void hangup() override { requests << QStringLiteral("hangup"); }
    void hold(bool on) override { requests << QStringLiteral("hold:%1").arg(on); }
    void deflect(const QString &target) override { requests << QStringLiteral("deflect:") + target; }
    void sendDtmf(const QString &tones) override { requests << QStringLiteral("dtmf:") + tones; }
    void merge(const QString &callHandle) override { requests << QStringLiteral("merge:") + callHandle; }
    void split() override { requests << QStringLiteral("split"); }
    void filter(VoiceCallFilterAction action) override
    {
        requests << QStringLiteral("filter:%1").arg(int(action));
    }

private:
    QString m_id;
    AbstractVoiceCallProvider *m_provider;
    bool m_incoming;
    VoiceCallStatus m_status;
    int m_duration = 0;
    bool m_emergency = false;
    QString m_lineId;
};

class FakeVoiceCallProvider : public AbstractVoiceCallProvider
{
    Q_OBJECT

public:
    explicit FakeVoiceCallProvider(const QString &id, QObject *parent = 0)
        : AbstractVoiceCallProvider(parent), m_id(id)
    {
    }

    QString providerId() const override { return m_id; }
    QString providerType() const override { return QStringLiteral("fake"); }
    QList<AbstractVoiceCallHandler *> voiceCalls() const override { return m_calls; }
    QString errorString() const override { return QString(); }

    // A call the provider already has, not announced (as before appendProvider)
    FakeVoiceCallHandler *createCall(const QString &id, bool incoming = false,
                                     AbstractVoiceCallHandler::VoiceCallStatus status
                                     = AbstractVoiceCallHandler::STATUS_NULL)
    {
        FakeVoiceCallHandler *handler = new FakeVoiceCallHandler(id, this, incoming, status);
        m_calls.append(handler);
        return handler;
    }

    // A new call, announced as a real provider does
    FakeVoiceCallHandler *addCall(const QString &id, bool incoming = false,
                                  AbstractVoiceCallHandler::VoiceCallStatus status
                                  = AbstractVoiceCallHandler::STATUS_NULL)
    {
        FakeVoiceCallHandler *handler = createCall(id, incoming, status);
        emit voiceCallAdded(handler);
        emit voiceCallsChanged();
        return handler;
    }

    // The call ended: announced, then deleted later as real providers do
    void removeCall(FakeVoiceCallHandler *handler)
    {
        m_calls.removeAll(handler);
        emit voiceCallRemoved(handler->handlerId());
        emit voiceCallsChanged();
        handler->deleteLater();
    }

public Q_SLOTS:
    bool dial(const QString &msisdn) override
    {
        dialled << msisdn;
        return true;
    }

public:
    QStringList dialled;

private:
    QString m_id;
    QList<AbstractVoiceCallHandler *> m_calls;
};

#endif // FAKE_VOICECALL_H
