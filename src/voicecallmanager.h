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
#ifndef VOICECALLMANAGER_H
#define VOICECALLMANAGER_H

#include <voicecallmanagerinterface.h>

class VoiceCallManager : public VoiceCallManagerInterface
{
    Q_OBJECT

public:
    explicit VoiceCallManager(QObject *parent = 0);
    ~VoiceCallManager();

    QList<AbstractVoiceCallProvider*> providers() const override;

    QString generateHandlerId() override;

    int voiceCallCount() const override;
    QList<AbstractVoiceCallHandler*> voiceCalls() const override;

    AbstractVoiceCallHandler* activeVoiceCall() const override;

    QString audioMode() const override;
    bool isAudioRouted() const override;
    bool isMicrophoneMuted() const override;
    bool isSpeakerMuted() const override;

    QString errorString() const override;

    int totalOutgoingCallDuration() const override;
    int totalIncomingCallDuration() const override;
    void resetCallDurationCounters() override;

public Q_SLOTS:
    void setError(const QString &errorString) override;

    void appendProvider(AbstractVoiceCallProvider *provider) override;
    void removeProvider(AbstractVoiceCallProvider *provider) override;

    bool dial(const QString &providerId, const QString &msisdn) override;

    void setCallFiltering(bool on = true) override;

    void playRingtone(const QString &ringtonePath) override;
    void silenceRingtone() override;

    void setAudioMode(const QString &mode) override;
    void setAudioRouted(bool on) override;
    void setMuteMicrophone(bool on) override;
    void setMuteSpeaker(bool on) override;

    void onAudioModeChanged(const QString &mode) override;
    void onAudioRoutedChanged(bool on) override;
    void onMuteMicrophoneChanged(bool on) override;
    void onMuteSpeakerChanged(bool on) override;

    void startEventTone(ToneType type, int volume) override;
    void stopEventTone() override;

    void startDtmfTone(const QString &tone, int volume) override;
    void stopDtmfTone() override;

protected Q_SLOTS:
    void onVoiceCallAdded(AbstractVoiceCallHandler *handler);
    void onVoiceCallRemoved(const QString &handlerId);
    void updateActiveVoiceCall();

private:
    void forgetVoiceCall(AbstractVoiceCallHandler *handler);

private:
    class VoiceCallManagerPrivate *d_ptr;

    Q_DECLARE_PRIVATE(VoiceCallManager)
};

#endif // VOICECALLMANAGER_H
