/*
 * This file is a part of the Voice Call Manager project
 *
 * Copyright (C) 2026 Herman van Hazendonk <github.com@herrie.org>
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
#include "voicecallmanager.h"

#include "fake_voicecall.h"

#include <QPointer>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

typedef AbstractVoiceCallHandler H;

class tst_VoiceCallManager : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();

    void appendProviderAnnouncesExistingCalls();
    void removeProviderForgetsItsCalls();
    void reappendedProviderIsNotDoubleCounted();
    void duplicateAnnouncementIgnored();
    void activeCallFollowsStatus();
    void activeCallReplacedWhenRemoved();
    void incomingCallReleasedWithoutFilter();
    void incomingCallHeldForFilter();
    void durationCounters();

private:
    QTemporaryDir m_home;
};

void tst_VoiceCallManager::initTestCase()
{
    // Keep the call counters (QSettings) out of the user's configuration,
    // and in a directory that can be written: Qt 5.6 reads back what it
    // failed to write as nothing (a runner without a home directory).
    QVERIFY(m_home.isValid());
    qputenv("HOME", QFile::encodeName(m_home.path()));
    qunsetenv("XDG_CONFIG_HOME");
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("voicecall-tests"));
    QCoreApplication::setApplicationName(QStringLiteral("tst_voicecallmanager"));
}

void tst_VoiceCallManager::init()
{
    QSettings().clear();
}

void tst_VoiceCallManager::appendProviderAnnouncesExistingCalls()
{
    VoiceCallManager manager;
    FakeVoiceCallProvider provider(QStringLiteral("p1"));
    provider.createCall(QStringLiteral("a"), false, H::STATUS_ACTIVE);
    provider.createCall(QStringLiteral("b"), false, H::STATUS_HELD);

    QSignalSpy added(&manager, SIGNAL(voiceCallAdded(AbstractVoiceCallHandler*)));
    manager.appendProvider(&provider);

    QCOMPARE(added.count(), 2);
    QCOMPARE(manager.voiceCallCount(), 2);
    QVERIFY(manager.activeVoiceCall());
    QCOMPARE(manager.activeVoiceCall()->handlerId(), QStringLiteral("a"));
}

// A provider that disappears mid-call (modem removed, telepathy account
// offline) used to leave its calls, and the active call, behind as dangling
// pointers once it deleted them.
void tst_VoiceCallManager::removeProviderForgetsItsCalls()
{
    VoiceCallManager manager;
    FakeVoiceCallProvider other(QStringLiteral("other"));
    manager.appendProvider(&other);
    FakeVoiceCallHandler *survivor = other.addCall(QStringLiteral("s"), false, H::STATUS_HELD);

    QPointer<FakeVoiceCallProvider> provider = new FakeVoiceCallProvider(QStringLiteral("p1"));
    manager.appendProvider(provider);
    provider->addCall(QStringLiteral("a"), false, H::STATUS_ACTIVE);
    provider->addCall(QStringLiteral("b"), true, H::STATUS_WAITING);
    QCOMPARE(manager.activeVoiceCall()->handlerId(), QStringLiteral("a"));

    QSignalSpy removed(&manager, SIGNAL(voiceCallRemoved(QString)));
    QSignalSpy activeChanged(&manager, SIGNAL(activeVoiceCallChanged()));
    manager.removeProvider(provider);

    QCOMPARE(removed.count(), 2);
    QStringList removedIds;
    removedIds << removed.at(0).at(0).toString() << removed.at(1).at(0).toString();
    removedIds.sort();
    QCOMPARE(removedIds, QStringList() << QStringLiteral("a") << QStringLiteral("b"));
    QVERIFY(activeChanged.count() >= 1);
    QCOMPARE(manager.activeVoiceCall(), static_cast<AbstractVoiceCallHandler *>(survivor));

    // The provider then deletes itself and its calls, as the ofono factory
    // does. Nothing in the manager may refer to them any more.
    delete provider.data();
    QCOMPARE(manager.activeVoiceCall(), static_cast<AbstractVoiceCallHandler *>(survivor));
    survivor->setStatus(H::STATUS_ACTIVE);
    QCOMPARE(manager.voiceCallCount(), 1);
}

// A telepathy account going offline and online again removes and re-appends
// its provider while calls survive. They must be announced once each time.
void tst_VoiceCallManager::reappendedProviderIsNotDoubleCounted()
{
    VoiceCallManager manager;
    FakeVoiceCallProvider provider(QStringLiteral("p1"));
    manager.appendProvider(&provider);
    provider.addCall(QStringLiteral("a"), false, H::STATUS_ACTIVE);

    manager.removeProvider(&provider);
    QVERIFY(!manager.activeVoiceCall());

    QSignalSpy added(&manager, SIGNAL(voiceCallAdded(AbstractVoiceCallHandler*)));
    manager.appendProvider(&provider);
    QCOMPARE(added.count(), 1);
    QCOMPARE(manager.activeVoiceCall()->handlerId(), QStringLiteral("a"));
}

void tst_VoiceCallManager::duplicateAnnouncementIgnored()
{
    VoiceCallManager manager;
    FakeVoiceCallProvider provider(QStringLiteral("p1"));
    manager.appendProvider(&provider);
    FakeVoiceCallHandler *call = provider.addCall(QStringLiteral("a"), true);

    QSignalSpy added(&manager, SIGNAL(voiceCallAdded(AbstractVoiceCallHandler*)));
    emit provider.voiceCallAdded(call);
    QCOMPARE(added.count(), 0);
    // ... and the call was released to ring only once
    QCOMPARE(call->requests, QStringList() << QStringLiteral("filter:0"));
}

void tst_VoiceCallManager::activeCallFollowsStatus()
{
    VoiceCallManager manager;
    FakeVoiceCallProvider provider(QStringLiteral("p1"));
    manager.appendProvider(&provider);
    FakeVoiceCallHandler *a = provider.addCall(QStringLiteral("a"), false, H::STATUS_ACTIVE);
    FakeVoiceCallHandler *b = provider.addCall(QStringLiteral("b"), true, H::STATUS_WAITING);
    QCOMPARE(manager.activeVoiceCall(), static_cast<H *>(a));

    // Answering the waiting call puts the first one on hold
    QSignalSpy activeChanged(&manager, SIGNAL(activeVoiceCallChanged()));
    a->setStatus(H::STATUS_HELD);
    b->setStatus(H::STATUS_ACTIVE);
    QCOMPARE(manager.activeVoiceCall(), static_cast<H *>(b));
    QVERIFY(activeChanged.count() >= 1);

    // Swapping back
    activeChanged.clear();
    b->setStatus(H::STATUS_HELD);
    a->setStatus(H::STATUS_ACTIVE);
    QCOMPARE(manager.activeVoiceCall(), static_cast<H *>(a));

    // Equal rank: the current choice is kept
    activeChanged.clear();
    b->setStatus(H::STATUS_ACTIVE);
    QCOMPARE(manager.activeVoiceCall(), static_cast<H *>(a));
    QCOMPARE(activeChanged.count(), 0);
}

void tst_VoiceCallManager::activeCallReplacedWhenRemoved()
{
    VoiceCallManager manager;
    FakeVoiceCallProvider provider(QStringLiteral("p1"));
    manager.appendProvider(&provider);
    FakeVoiceCallHandler *a = provider.addCall(QStringLiteral("a"), false, H::STATUS_ACTIVE);
    FakeVoiceCallHandler *b = provider.addCall(QStringLiteral("b"), false, H::STATUS_HELD);

    // The remaining held call takes over, rather than none at all
    provider.removeCall(a);
    QCOMPARE(manager.activeVoiceCall(), static_cast<H *>(b));

    provider.removeCall(b);
    QVERIFY(!manager.activeVoiceCall());
    QCoreApplication::sendPostedEvents(Q_NULLPTR, QEvent::DeferredDelete);
    QVERIFY(!manager.activeVoiceCall());
}

void tst_VoiceCallManager::incomingCallReleasedWithoutFilter()
{
    VoiceCallManager manager;
    FakeVoiceCallProvider provider(QStringLiteral("p1"));
    manager.appendProvider(&provider);

    FakeVoiceCallHandler *incoming = provider.addCall(QStringLiteral("in"), true);
    FakeVoiceCallHandler *outgoing = provider.addCall(QStringLiteral("out"), false);
    QCOMPARE(incoming->requests, QStringList() << QStringLiteral("filter:0"));
    QVERIFY(outgoing->requests.isEmpty());
}

void tst_VoiceCallManager::incomingCallHeldForFilter()
{
    VoiceCallManager manager;
    manager.setCallFiltering(true);
    FakeVoiceCallProvider provider(QStringLiteral("p1"));
    manager.appendProvider(&provider);

    FakeVoiceCallHandler *incoming = provider.addCall(QStringLiteral("in"), true);
    QVERIFY(incoming->requests.isEmpty());
}

void tst_VoiceCallManager::durationCounters()
{
    VoiceCallManager manager;
    FakeVoiceCallProvider provider(QStringLiteral("p1"));
    manager.appendProvider(&provider);

    FakeVoiceCallHandler *in = provider.addCall(QStringLiteral("in"), true, H::STATUS_ACTIVE);
    FakeVoiceCallHandler *out = provider.addCall(QStringLiteral("out"), false, H::STATUS_ACTIVE);
    in->setDuration(12);
    out->setDuration(30);

    QSignalSpy incomingChanged(&manager, SIGNAL(totalIncomingCallDurationChanged()));
    provider.removeCall(in);
    provider.removeCall(out);
    QCOMPARE(incomingChanged.count(), 1);
    QCOMPARE(manager.totalIncomingCallDuration(), 12);
    QCOMPARE(manager.totalOutgoingCallDuration(), 30);

    // Removing a call that isn't registered changes nothing
    emit provider.voiceCallRemoved(QStringLiteral("unknown"));
    QCOMPARE(manager.totalIncomingCallDuration(), 12);

    manager.resetCallDurationCounters();
    QCOMPARE(manager.totalIncomingCallDuration(), 0);
    QCOMPARE(manager.totalOutgoingCallDuration(), 0);
}

QTEST_GUILESS_MAIN(tst_VoiceCallManager)

#include "tst_voicecallmanager.moc"
