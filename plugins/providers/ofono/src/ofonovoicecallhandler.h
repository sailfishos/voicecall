/*
 * This file is a part of the Voice Call Manager Ofono Plugin project.
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
#ifndef OFONOVOICECALLHANDLER_H
#define OFONOVOICECALLHANDLER_H

#include <abstractvoicecallhandler.h>

class OfonoVoiceCallProvider;
class QOfonoVoiceCallManager;

class OfonoVoiceCallHandler : public AbstractVoiceCallHandler
{
    Q_OBJECT

    Q_PROPERTY(QString path READ path)

public:
    explicit OfonoVoiceCallHandler(const QString &handlerId, const QString &path, OfonoVoiceCallProvider *provider,
                                   QOfonoVoiceCallManager *manager);
    ~OfonoVoiceCallHandler();

    QString path() const;

    AbstractVoiceCallProvider* provider() const override;

    QString handlerId() const override;
    QString lineId() const override;
    QString subscriberId() const override;
    QDateTime startedAt() const override;
    int duration() const override;
    bool isIncoming() const override;
    bool isMultiparty() const override;
    bool isEmergency() const override;
    bool isForwarded() const override;
    bool isRemoteHeld() const override;

    // TODO: unimplemented - JB#35997
    QString parentHandlerId() const override { return QString(); }
    QList<AbstractVoiceCallHandler*> childCalls() const override { return QList<AbstractVoiceCallHandler*>(); }

    VoiceCallStatus status() const override;

Q_SIGNALS:
    void validChanged(bool valid);

public Q_SLOTS:
    void answer() override;
    void hangup() override;
    void hold(bool on = true) override;
    void deflect(const QString &target) override;
    void sendDtmf(const QString &tones) override;
    void filter(VoiceCallFilterAction action) override;
    void merge(const QString &callHandle) override;
    void split() override;

protected Q_SLOTS:
    void onStatusChanged();
    void onValidChanged(bool);

protected:
    void timerEvent(QTimerEvent *event) override;

private:
    class OfonoVoiceCallHandlerPrivate *d_ptr;

    Q_DECLARE_PRIVATE(OfonoVoiceCallHandler)
};

#endif // OFONOVOICECALLHANDLER_H
