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

#include <QTest>
#include <QDebug>
#include <QObject>
#include <QTemporaryDir>
#include <QtGlobal>

#include <filter.h>

#include <abstractvoicecallprovider.h>

class Provider: public AbstractVoiceCallProvider
{
    Q_OBJECT
public:
    Provider(QObject *parent = nullptr)
        : AbstractVoiceCallProvider(parent)
    {
    }
    QString providerId() const override
    {
        return QStringLiteral("/org/freedesktop/Telepathy/Account/ring/tel");
    }
    QString providerType() const override
    {
        return QString();
    }
    QList<AbstractVoiceCallHandler*> voiceCalls() const override
    {
        return QList<AbstractVoiceCallHandler*>();
    }
    QString errorString() const override
    {
        return QString();
    }
    bool dial(const QString &msisdn) override
    {
        Q_UNUSED(msisdn);
        return false;
    }
};

class Call: public AbstractVoiceCallHandler
{
public:
    Call(AbstractVoiceCallProvider *provider, const QString &number)
        : m_lineId(number)
        , m_provider(provider)
    {
    }
    AbstractVoiceCallProvider* provider() const override
    {
        return m_provider;
    }
    QString handlerId() const override
    {
        return QString();
    }
    QString lineId() const override
    {
        return m_lineId;
    }
    QString subscriberId() const override
    {
        return QString();
    }
    QDateTime startedAt() const override
    {
        return QDateTime();
    }
    int duration() const override
    {
        return 0;
    }
    bool isIncoming() const override
    {
        return true;
    }
    bool isMultiparty() const override
    {
        return false;
    }
    bool isEmergency() const override
    {
        return false;
    }
    bool isForwarded() const override
    {
        return false;
    }
    bool isRemoteHeld() const override
    {
        return false;
    }
    QString parentHandlerId() const override
    {
        return QString();
    }
    QList<AbstractVoiceCallHandler*> childCalls() const override
    {
        return QList<AbstractVoiceCallHandler*>();
    }
    VoiceCallStatus status() const override
    {
        return STATUS_NULL;
    }
    void answer() override
    {
    }
    void hangup() override
    {
    }  
    void hold(bool on) override
    {
        Q_UNUSED(on);
    }
    void deflect(const QString &target) override
    {
        Q_UNUSED(target);
    }
    void sendDtmf(const QString &tones) override
    {
        Q_UNUSED(tones);
    }
    void merge(const QString &callHandle) override
    {
        Q_UNUSED(callHandle);
    }
    void split() override
    {
    }
    void filter(VoiceCallFilterAction action) override
    {
        Q_UNUSED(action);
    }

private:
    QString m_lineId;
    AbstractVoiceCallProvider *m_provider;
};

class tst_filter: public QObject
{
    Q_OBJECT

public:
    tst_filter(QObject *parent = nullptr);

private slots:
    void initTestCase();

    void tst_ignoreNumbers();
    void tst_ignorePrefix();
    void tst_overrideIgnorePrefix();

    void tst_rejectNumbers();
    void tst_rejectPrefix();
    void tst_overrideRejectPrefix();

    void tst_overrideAcceptPrefix();

private:
    QTemporaryDir mTmpHome;
    Provider mProvider;
};

tst_filter::tst_filter(QObject *parent)
    : QObject(parent)
{
}

void tst_filter::initTestCase()
{
    // This is used not to damage the user DConf database.
    if (mTmpHome.isValid()) {
        qputenv("HOME", mTmpHome.path().toUtf8());
    }
}

void tst_filter::tst_ignoreNumbers()
{
    VoiceCall::Filter filter;
    const QString number1 = QStringLiteral("+33555789100");
    const QString number2 = QStringLiteral("0123456789");
    const QString number3 = QStringLiteral("0555789100");
    const Call call1(&mProvider, number1);
    const Call call2(&mProvider, number2);
    const Call call3(&mProvider, number3);

    QVERIFY(filter.ignoredList().isEmpty());

    filter.ignoreNumber(number1);
    QCOMPARE(filter.ignoredList().count(), 1);
    QCOMPARE(filter.ignoredList().first(), number1);
    QCOMPARE(filter.evaluate(call1), AbstractVoiceCallHandler::ACTION_IGNORE);
    QCOMPARE(filter.evaluate(call2), AbstractVoiceCallHandler::ACTION_CONTINUE);
    QCOMPARE(filter.evaluate(call3), AbstractVoiceCallHandler::ACTION_IGNORE);

    filter.ignoreNumber(number2);
    QCOMPARE(filter.ignoredList().count(), 2);
    QCOMPARE(filter.ignoredList().first(), number2);
    QCOMPARE(filter.evaluate(call1), AbstractVoiceCallHandler::ACTION_IGNORE);
    QCOMPARE(filter.evaluate(call2), AbstractVoiceCallHandler::ACTION_IGNORE);
    QCOMPARE(filter.evaluate(call3), AbstractVoiceCallHandler::ACTION_IGNORE);

    filter.acceptNumber(number1);
    QCOMPARE(filter.ignoredList().count(), 1);
    QCOMPARE(filter.ignoredList().first(), number2);
    QCOMPARE(filter.evaluate(call1), AbstractVoiceCallHandler::ACTION_CONTINUE);
    QCOMPARE(filter.evaluate(call2), AbstractVoiceCallHandler::ACTION_IGNORE);
    QCOMPARE(filter.evaluate(call3), AbstractVoiceCallHandler::ACTION_CONTINUE);

    filter.acceptNumber(number2);
    QVERIFY(filter.ignoredList().isEmpty());
    QCOMPARE(filter.evaluate(call1), AbstractVoiceCallHandler::ACTION_CONTINUE);
    QCOMPARE(filter.evaluate(call2), AbstractVoiceCallHandler::ACTION_CONTINUE);
    QCOMPARE(filter.evaluate(call3), AbstractVoiceCallHandler::ACTION_CONTINUE);
    
    filter.ignoreNumber(number3);
    QCOMPARE(filter.ignoredList().count(), 1);
    QCOMPARE(filter.ignoredList().first(), number3);
    QCOMPARE(filter.evaluate(call1), AbstractVoiceCallHandler::ACTION_IGNORE);
    QCOMPARE(filter.evaluate(call2), AbstractVoiceCallHandler::ACTION_CONTINUE);
    QCOMPARE(filter.evaluate(call3), AbstractVoiceCallHandler::ACTION_IGNORE);

    filter.acceptNumber(number3);
    QVERIFY(filter.ignoredList().isEmpty());
    QCOMPARE(filter.evaluate(call1), AbstractVoiceCallHandler::ACTION_CONTINUE);
    QCOMPARE(filter.evaluate(call2), AbstractVoiceCallHandler::ACTION_CONTINUE);
    QCOMPARE(filter.evaluate(call3), AbstractVoiceCallHandler::ACTION_CONTINUE);
}

void tst_filter::tst_ignorePrefix()
{
    VoiceCall::Filter filter;
    const QString prefix1 = QStringLiteral("+1555");
    const Call call1(&mProvider, QStringLiteral("+155578910"));
    const Call call2(&mProvider, QStringLiteral("+155523456"));
    const Call call3(&mProvider, QStringLiteral("+155400000"));

    QVERIFY(filter.ignoredList().isEmpty());

    filter.ignoreNumbersStartingWith(prefix1);
    QCOMPARE(filter.ignoredList().count(), 1);
    QCOMPARE(filter.ignoredList().first(), QStringLiteral("^") + prefix1);
    QCOMPARE(filter.evaluate(call1), AbstractVoiceCallHandler::ACTION_IGNORE);
    QCOMPARE(filter.evaluate(call2), AbstractVoiceCallHandler::ACTION_IGNORE);
    QCOMPARE(filter.evaluate(call3), AbstractVoiceCallHandler::ACTION_CONTINUE);

    filter.acceptNumbersStartingWith(prefix1);
    QVERIFY(filter.ignoredList().isEmpty());
    QCOMPARE(filter.evaluate(call1), AbstractVoiceCallHandler::ACTION_CONTINUE);
    QCOMPARE(filter.evaluate(call2), AbstractVoiceCallHandler::ACTION_CONTINUE);
    QCOMPARE(filter.evaluate(call3), AbstractVoiceCallHandler::ACTION_CONTINUE);
}

void tst_filter::tst_overrideIgnorePrefix()
{
    VoiceCall::Filter filter;
    const QString prefix1 = QStringLiteral("+1555");
    const QString number1 = QStringLiteral("+155578910");
    const Call call1(&mProvider, number1);
    const Call call2(&mProvider, QStringLiteral("+155523456"));
    const Call call3(&mProvider, QStringLiteral("+155400000"));

    QVERIFY(filter.ignoredList().isEmpty());
    QVERIFY(filter.rejectedList().isEmpty());
    QVERIFY(filter.whiteList().isEmpty());

    filter.ignoreNumbersStartingWith(prefix1);
    filter.rejectNumber(number1);
    QCOMPARE(filter.ignoredList().count(), 1);
    QCOMPARE(filter.ignoredList().first(), QStringLiteral("^") + prefix1);
    QCOMPARE(filter.rejectedList().count(), 1);
    QCOMPARE(filter.rejectedList().first(), number1);
    QCOMPARE(filter.evaluate(call1), AbstractVoiceCallHandler::ACTION_REJECT);
    QCOMPARE(filter.evaluate(call2), AbstractVoiceCallHandler::ACTION_IGNORE);
    QCOMPARE(filter.evaluate(call3), AbstractVoiceCallHandler::ACTION_CONTINUE);

    filter.acceptNumber(number1);
    QVERIFY(filter.rejectedList().isEmpty());
    QCOMPARE(filter.whiteList().count(), 1);
    QCOMPARE(filter.whiteList().first(), number1);
    QCOMPARE(filter.evaluate(call1), AbstractVoiceCallHandler::ACTION_CONTINUE);
    QCOMPARE(filter.evaluate(call2), AbstractVoiceCallHandler::ACTION_IGNORE);
    QCOMPARE(filter.evaluate(call3), AbstractVoiceCallHandler::ACTION_CONTINUE);

    filter.ignoreNumber(number1);
    QVERIFY(filter.rejectedList().isEmpty());
    QVERIFY(filter.whiteList().isEmpty());
    QCOMPARE(filter.ignoredList().count(), 2);
    QCOMPARE(filter.ignoredList().first(), number1);
    QCOMPARE(filter.evaluate(call1), AbstractVoiceCallHandler::ACTION_IGNORE);
    QCOMPARE(filter.evaluate(call2), AbstractVoiceCallHandler::ACTION_IGNORE);
    QCOMPARE(filter.evaluate(call3), AbstractVoiceCallHandler::ACTION_CONTINUE);

    filter.acceptNumbersStartingWith(prefix1);
    filter.acceptNumber(number1);
    QVERIFY(filter.ignoredList().isEmpty());
    QVERIFY(filter.rejectedList().isEmpty());
    QVERIFY(filter.whiteList().isEmpty());
    QCOMPARE(filter.evaluate(call1), AbstractVoiceCallHandler::ACTION_CONTINUE);
    QCOMPARE(filter.evaluate(call2), AbstractVoiceCallHandler::ACTION_CONTINUE);
    QCOMPARE(filter.evaluate(call3), AbstractVoiceCallHandler::ACTION_CONTINUE);
}


void tst_filter::tst_rejectNumbers()
{
    VoiceCall::Filter filter;
    const QString number1 = QStringLiteral("+33555789100");
    const QString number2 = QStringLiteral("0123456789");
    const QString number3 = QStringLiteral("0555789100");
    const Call call1(&mProvider, number1);
    const Call call2(&mProvider, number2);
    const Call call3(&mProvider, number3);

    QVERIFY(filter.rejectedList().isEmpty());

    filter.rejectNumber(number1);
    QCOMPARE(filter.rejectedList().count(), 1);
    QCOMPARE(filter.rejectedList().first(), number1);
    QCOMPARE(filter.evaluate(call1), AbstractVoiceCallHandler::ACTION_REJECT);
    QCOMPARE(filter.evaluate(call2), AbstractVoiceCallHandler::ACTION_CONTINUE);
    QCOMPARE(filter.evaluate(call3), AbstractVoiceCallHandler::ACTION_REJECT);

    filter.rejectNumber(number2);
    QCOMPARE(filter.rejectedList().count(), 2);
    QCOMPARE(filter.rejectedList().first(), number2);
    QCOMPARE(filter.evaluate(call1), AbstractVoiceCallHandler::ACTION_REJECT);
    QCOMPARE(filter.evaluate(call2), AbstractVoiceCallHandler::ACTION_REJECT);
    QCOMPARE(filter.evaluate(call3), AbstractVoiceCallHandler::ACTION_REJECT);

    filter.acceptNumber(number1);
    QCOMPARE(filter.rejectedList().count(), 1);
    QCOMPARE(filter.rejectedList().first(), number2);
    QCOMPARE(filter.evaluate(call1), AbstractVoiceCallHandler::ACTION_CONTINUE);
    QCOMPARE(filter.evaluate(call2), AbstractVoiceCallHandler::ACTION_REJECT);
    QCOMPARE(filter.evaluate(call3), AbstractVoiceCallHandler::ACTION_CONTINUE);

    filter.acceptNumber(number2);
    QVERIFY(filter.rejectedList().isEmpty());
    QCOMPARE(filter.evaluate(call1), AbstractVoiceCallHandler::ACTION_CONTINUE);
    QCOMPARE(filter.evaluate(call2), AbstractVoiceCallHandler::ACTION_CONTINUE);
    QCOMPARE(filter.evaluate(call3), AbstractVoiceCallHandler::ACTION_CONTINUE);

    filter.rejectNumber(number3);
    QCOMPARE(filter.rejectedList().count(), 1);
    QCOMPARE(filter.rejectedList().first(), number3);
    QCOMPARE(filter.evaluate(call1), AbstractVoiceCallHandler::ACTION_REJECT);
    QCOMPARE(filter.evaluate(call2), AbstractVoiceCallHandler::ACTION_CONTINUE);
    QCOMPARE(filter.evaluate(call3), AbstractVoiceCallHandler::ACTION_REJECT);

    filter.acceptNumber(number3);
    QVERIFY(filter.rejectedList().isEmpty());
    QCOMPARE(filter.evaluate(call1), AbstractVoiceCallHandler::ACTION_CONTINUE);
    QCOMPARE(filter.evaluate(call2), AbstractVoiceCallHandler::ACTION_CONTINUE);
    QCOMPARE(filter.evaluate(call3), AbstractVoiceCallHandler::ACTION_CONTINUE);
}

void tst_filter::tst_rejectPrefix()
{
    VoiceCall::Filter filter;
    const QString prefix1 = QStringLiteral("+1555");
    const Call call1(&mProvider, QStringLiteral("+155578910"));
    const Call call2(&mProvider, QStringLiteral("+155523456"));
    const Call call3(&mProvider, QStringLiteral("+155400000"));

    QVERIFY(filter.rejectedList().isEmpty());

    filter.rejectNumbersStartingWith(prefix1);
    QCOMPARE(filter.rejectedList().count(), 1);
    QCOMPARE(filter.rejectedList().first(), QStringLiteral("^") + prefix1);
    QCOMPARE(filter.evaluate(call1), AbstractVoiceCallHandler::ACTION_REJECT);
    QCOMPARE(filter.evaluate(call2), AbstractVoiceCallHandler::ACTION_REJECT);
    QCOMPARE(filter.evaluate(call3), AbstractVoiceCallHandler::ACTION_CONTINUE);

    filter.acceptNumbersStartingWith(prefix1);
    QVERIFY(filter.rejectedList().isEmpty());
    QCOMPARE(filter.evaluate(call1), AbstractVoiceCallHandler::ACTION_CONTINUE);
    QCOMPARE(filter.evaluate(call2), AbstractVoiceCallHandler::ACTION_CONTINUE);
    QCOMPARE(filter.evaluate(call3), AbstractVoiceCallHandler::ACTION_CONTINUE);
}

void tst_filter::tst_overrideRejectPrefix()
{
    VoiceCall::Filter filter;
    const QString prefix1 = QStringLiteral("+1555");
    const QString number1 = QStringLiteral("+155578910");
    const Call call1(&mProvider, number1);
    const Call call2(&mProvider, QStringLiteral("+155523456"));
    const Call call3(&mProvider, QStringLiteral("+155400000"));

    QVERIFY(filter.ignoredList().isEmpty());
    QVERIFY(filter.rejectedList().isEmpty());
    QVERIFY(filter.whiteList().isEmpty());

    filter.rejectNumbersStartingWith(prefix1);
    filter.ignoreNumber(number1);
    QCOMPARE(filter.rejectedList().count(), 1);
    QCOMPARE(filter.rejectedList().first(), QStringLiteral("^") + prefix1);
    QCOMPARE(filter.ignoredList().count(), 1);
    QCOMPARE(filter.ignoredList().first(), number1);
    QCOMPARE(filter.evaluate(call1), AbstractVoiceCallHandler::ACTION_IGNORE);
    QCOMPARE(filter.evaluate(call2), AbstractVoiceCallHandler::ACTION_REJECT);
    QCOMPARE(filter.evaluate(call3), AbstractVoiceCallHandler::ACTION_CONTINUE);

    filter.acceptNumber(number1);
    QVERIFY(filter.ignoredList().isEmpty());
    QCOMPARE(filter.whiteList().count(), 1);
    QCOMPARE(filter.whiteList().first(), number1);
    QCOMPARE(filter.evaluate(call1), AbstractVoiceCallHandler::ACTION_CONTINUE);
    QCOMPARE(filter.evaluate(call2), AbstractVoiceCallHandler::ACTION_REJECT);
    QCOMPARE(filter.evaluate(call3), AbstractVoiceCallHandler::ACTION_CONTINUE);

    filter.rejectNumber(number1);
    QVERIFY(filter.ignoredList().isEmpty());
    QVERIFY(filter.whiteList().isEmpty());
    QCOMPARE(filter.rejectedList().count(), 2);
    QCOMPARE(filter.rejectedList().first(), number1);
    QCOMPARE(filter.evaluate(call1), AbstractVoiceCallHandler::ACTION_REJECT);
    QCOMPARE(filter.evaluate(call2), AbstractVoiceCallHandler::ACTION_REJECT);
    QCOMPARE(filter.evaluate(call3), AbstractVoiceCallHandler::ACTION_CONTINUE);

    filter.acceptNumbersStartingWith(prefix1);
    filter.acceptNumber(number1);
    QVERIFY(filter.ignoredList().isEmpty());
    QVERIFY(filter.rejectedList().isEmpty());
    QVERIFY(filter.whiteList().isEmpty());
    QCOMPARE(filter.evaluate(call1), AbstractVoiceCallHandler::ACTION_CONTINUE);
    QCOMPARE(filter.evaluate(call2), AbstractVoiceCallHandler::ACTION_CONTINUE);
    QCOMPARE(filter.evaluate(call3), AbstractVoiceCallHandler::ACTION_CONTINUE);
}


void tst_filter::tst_overrideAcceptPrefix()
{
    VoiceCall::Filter filter;
    const QString prefix1 = QStringLiteral("+1555");
    const QString prefix2 = QStringLiteral("+1");
    const Call call1(&mProvider, QStringLiteral("+155578910"));
    const Call call2(&mProvider, QStringLiteral("+155523456"));
    const Call call3(&mProvider, QStringLiteral("+155400000"));

    QVERIFY(filter.ignoredList().isEmpty());
    QVERIFY(filter.rejectedList().isEmpty());
    QVERIFY(filter.whiteList().isEmpty());

    filter.ignoreByDefault();
    filter.acceptNumbersStartingWith(prefix1);
    QCOMPARE(filter.whiteList().count(), 1);
    QCOMPARE(filter.whiteList().first(), QStringLiteral("^") + prefix1);
    QCOMPARE(filter.ignoredList().count(), 1);
    QCOMPARE(filter.ignoredList().first(), QStringLiteral("*"));
    QCOMPARE(filter.evaluate(call1), AbstractVoiceCallHandler::ACTION_CONTINUE);
    QCOMPARE(filter.evaluate(call2), AbstractVoiceCallHandler::ACTION_CONTINUE);
    QCOMPARE(filter.evaluate(call3), AbstractVoiceCallHandler::ACTION_IGNORE);

    filter.rejectByDefault();
    QVERIFY(filter.ignoredList().isEmpty());
    QCOMPARE(filter.rejectedList().count(), 1);
    QCOMPARE(filter.rejectedList().first(), QStringLiteral("*"));
    QCOMPARE(filter.evaluate(call1), AbstractVoiceCallHandler::ACTION_CONTINUE);
    QCOMPARE(filter.evaluate(call2), AbstractVoiceCallHandler::ACTION_CONTINUE);
    QCOMPARE(filter.evaluate(call3), AbstractVoiceCallHandler::ACTION_REJECT);

    filter.acceptByDefault();
    QVERIFY(filter.ignoredList().isEmpty());
    QVERIFY(filter.rejectedList().isEmpty());
    QCOMPARE(filter.evaluate(call1), AbstractVoiceCallHandler::ACTION_CONTINUE);
    QCOMPARE(filter.evaluate(call2), AbstractVoiceCallHandler::ACTION_CONTINUE);
    QCOMPARE(filter.evaluate(call3), AbstractVoiceCallHandler::ACTION_CONTINUE);

    filter.ignoreNumbersStartingWith(prefix2);
    QCOMPARE(filter.ignoredList().count(), 1);
    QCOMPARE(filter.ignoredList().first(), QStringLiteral("^") + prefix2);
    QCOMPARE(filter.evaluate(call1), AbstractVoiceCallHandler::ACTION_CONTINUE);
    QCOMPARE(filter.evaluate(call2), AbstractVoiceCallHandler::ACTION_CONTINUE);
    QCOMPARE(filter.evaluate(call3), AbstractVoiceCallHandler::ACTION_IGNORE);

    filter.rejectNumbersStartingWith(prefix2);
    QCOMPARE(filter.rejectedList().count(), 1);
    QCOMPARE(filter.rejectedList().first(), QStringLiteral("^") + prefix2);
    QCOMPARE(filter.evaluate(call1), AbstractVoiceCallHandler::ACTION_CONTINUE);
    QCOMPARE(filter.evaluate(call2), AbstractVoiceCallHandler::ACTION_CONTINUE);
    QCOMPARE(filter.evaluate(call3), AbstractVoiceCallHandler::ACTION_REJECT);

    filter.acceptNumbersStartingWith(prefix2);
    QVERIFY(filter.ignoredList().isEmpty());
    QVERIFY(filter.rejectedList().isEmpty());
    QCOMPARE(filter.whiteList().count(), 1);
    QCOMPARE(filter.whiteList().first(), QStringLiteral("^") + prefix1);
    QCOMPARE(filter.evaluate(call1), AbstractVoiceCallHandler::ACTION_CONTINUE);
    QCOMPARE(filter.evaluate(call2), AbstractVoiceCallHandler::ACTION_CONTINUE);
    QCOMPARE(filter.evaluate(call3), AbstractVoiceCallHandler::ACTION_CONTINUE);

    filter.acceptAll();
    QVERIFY(filter.ignoredList().isEmpty());
    QVERIFY(filter.rejectedList().isEmpty());
    QVERIFY(filter.whiteList().isEmpty());
    QCOMPARE(filter.evaluate(call1), AbstractVoiceCallHandler::ACTION_CONTINUE);
    QCOMPARE(filter.evaluate(call2), AbstractVoiceCallHandler::ACTION_CONTINUE);
    QCOMPARE(filter.evaluate(call3), AbstractVoiceCallHandler::ACTION_CONTINUE);
}

#include "tst_filter.moc"
QTEST_MAIN(tst_filter)

