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

// The ofono provider and call handler against fake libqofono objects.

#include "ofonovoicecallhandler.h"
#include "ofonovoicecallprovider.h"
#include "ofonovoicecallproviderfactory.h"
#include "voicecallmanager.h"

#include "fake_qofono.h"

#include <QPointer>
#include <QSignalSpy>
#include <QTimeZone>
#include <QtTest>

typedef AbstractVoiceCallHandler H;

static const char ModemPath[] = "/ril_0";

class tst_OfonoProvider : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void cleanup();

    void incomingCallWaitsForFilter();
    void continuedCallEndsDisconnected();
    void rejectedCallStaysRejected();
    void waitingCallIsIncoming();
    void directionSurvivesReconnect();
    void durationInSeconds();
    void startTimeParsed();
    void startTimeParsed_data();
    void dialFailureReported();
    void duplicateCallAddedIgnored();
    void interfaceLossDeletesPendingCalls();
    void callValidAtCreationAnnounced();
    void mergeJoinsActiveAndHeld();
    void mergeNeedsActiveAndHeld();
    void mergeFailureReported();
    void splitNeedsConference();
    void callControlAfterInterfaceLoss();
    void mergeAcrossModemsRefused();
    void modemLossLeavesOtherModem();
    void factoryTakesModemsListedLater();
    void factoryTakesModemsKnownAtStart();
    void factoryFollowsModemChanges();

private:
    OfonoVoiceCallProvider *createProvider(const char *modemPath = ModemPath);
    OfonoVoiceCallHandler *addValidCall(const QString &path, const QString &state);
    static OfonoVoiceCallHandler *addValidCall(OfonoVoiceCallProvider *provider,
                                               QOfonoVoiceCallManager *manager,
                                               const QString &path, const QString &state);

    VoiceCallManager *m_manager = 0;
    OfonoVoiceCallProvider *m_provider = 0;
};

void tst_OfonoProvider::init()
{
    QOfonoModem::initialInterfaces() = QStringList() << QStringLiteral("org.ofono.VoiceCallManager");
    QOfonoVoiceCall::presetStates().clear();
    QOfonoManager::initialModems().clear();
    m_manager = new VoiceCallManager;
    m_provider = createProvider();
}

void tst_OfonoProvider::cleanup()
{
    delete m_provider;
    m_provider = 0;
    delete m_manager;
    m_manager = 0;
}

OfonoVoiceCallProvider *tst_OfonoProvider::createProvider(const char *modemPath)
{
    OfonoVoiceCallProvider *provider = new OfonoVoiceCallProvider(QLatin1String(modemPath), m_manager);
    m_manager->appendProvider(provider);
    return provider;
}

OfonoVoiceCallHandler *tst_OfonoProvider::addValidCall(const QString &path, const QString &state)
{
    return addValidCall(m_provider, QOfonoVoiceCallManager::last(), path, state);
}

OfonoVoiceCallHandler *tst_OfonoProvider::addValidCall(OfonoVoiceCallProvider *provider,
                                                       QOfonoVoiceCallManager *manager,
                                                       const QString &path, const QString &state)
{
    emit manager->callAdded(path);
    QOfonoVoiceCall *call = QOfonoVoiceCall::instances().value(path);
    if (!call)
        return 0;
    call->becomeValid(state);
    foreach (AbstractVoiceCallHandler *handler, provider->voiceCalls()) {
        OfonoVoiceCallHandler *ofonoHandler = qobject_cast<OfonoVoiceCallHandler *>(handler);
        if (ofonoHandler && ofonoHandler->path() == path)
            return ofonoHandler;
    }
    return 0;
}

void tst_OfonoProvider::incomingCallWaitsForFilter()
{
    m_manager->setCallFiltering(true);
    OfonoVoiceCallHandler *handler = addValidCall(QStringLiteral("/ril_0/voicecall01"),
                                                  QStringLiteral("incoming"));
    QVERIFY(handler);
    QVERIFY(handler->isIncoming());
    QCOMPARE(handler->status(), H::STATUS_NULL);

    handler->filter(H::ACTION_CONTINUE);
    QCOMPARE(handler->status(), H::STATUS_INCOMING);
}

// A call the filter let through used to report STATUS_INCOMING again when
// it ended, so plugins saw it ringing instead of disconnected.
void tst_OfonoProvider::continuedCallEndsDisconnected()
{
    OfonoVoiceCallHandler *handler = addValidCall(QStringLiteral("/ril_0/voicecall01"),
                                                  QStringLiteral("incoming"));
    QVERIFY(handler);
    // No call filtering: the manager released the call
    QCOMPARE(handler->status(), H::STATUS_INCOMING);

    QOfonoVoiceCall *call = QOfonoVoiceCall::instances().value(QStringLiteral("/ril_0/voicecall01"));
    QList<H::VoiceCallStatus> reported;
    connect(handler, &H::statusChanged, this, [&reported](H::VoiceCallStatus status) {
        reported << status;
    });
    call->changeState(QStringLiteral("active"));
    QCOMPARE(handler->status(), H::STATUS_ACTIVE);
    call->changeState(QStringLiteral("disconnected"));
    QCOMPARE(handler->status(), H::STATUS_DISCONNECTED);
    QCOMPARE(reported, QList<H::VoiceCallStatus>() << H::STATUS_ACTIVE << H::STATUS_DISCONNECTED);
}

void tst_OfonoProvider::rejectedCallStaysRejected()
{
    m_manager->setCallFiltering(true);
    OfonoVoiceCallHandler *handler = addValidCall(QStringLiteral("/ril_0/voicecall01"),
                                                  QStringLiteral("incoming"));
    QVERIFY(handler);
    handler->filter(H::ACTION_REJECT);
    QOfonoVoiceCall *call = QOfonoVoiceCall::instances().value(QStringLiteral("/ril_0/voicecall01"));
    QCOMPARE(call->requests, QStringList() << QStringLiteral("hangup"));
    QCOMPARE(handler->status(), H::STATUS_REJECTED);
    call->changeState(QStringLiteral("disconnected"));
    QCOMPARE(handler->status(), H::STATUS_REJECTED);
}

// A call waiting behind another one is mobile terminated: it was logged as
// outgoing and counted as dialled.
void tst_OfonoProvider::waitingCallIsIncoming()
{
    addValidCall(QStringLiteral("/ril_0/voicecall01"), QStringLiteral("active"));
    OfonoVoiceCallHandler *waiting = addValidCall(QStringLiteral("/ril_0/voicecall02"),
                                                  QStringLiteral("waiting"));
    QVERIFY(waiting);
    QVERIFY(waiting->isIncoming());
    QCOMPARE(waiting->status(), H::STATUS_WAITING);

    // Answering a waiting call holds the other one
    waiting->answer();
    QCOMPARE(QOfonoVoiceCallManager::last()->requests, QStringList() << QStringLiteral("holdAndAnswer"));
}

void tst_OfonoProvider::directionSurvivesReconnect()
{
    OfonoVoiceCallHandler *handler = addValidCall(QStringLiteral("/ril_0/voicecall01"),
                                                  QStringLiteral("incoming"));
    QVERIFY(handler);
    QOfonoVoiceCall *call = QOfonoVoiceCall::instances().value(QStringLiteral("/ril_0/voicecall01"));
    call->changeState(QStringLiteral("active"));
    // ofono restarting mid-call: valid again, now in state "active"
    call->becomeInvalid();
    call->becomeValid(QStringLiteral("active"));
    QVERIFY(handler->isIncoming());
}

// durationChanged() carried milliseconds while duration() is in seconds.
void tst_OfonoProvider::durationInSeconds()
{
    OfonoVoiceCallHandler *handler = addValidCall(QStringLiteral("/ril_0/voicecall01"),
                                                  QStringLiteral("dialing"));
    QVERIFY(handler);
    QSignalSpy duration(handler, SIGNAL(durationChanged(int)));
    QOfonoVoiceCall::instances().value(QStringLiteral("/ril_0/voicecall01"))
            ->changeState(QStringLiteral("active"));
    QTRY_VERIFY_WITH_TIMEOUT(duration.count() >= 1, 3000);
    const int reported = duration.first().at(0).toInt();
    QVERIFY2(reported >= 0 && reported <= 2, qPrintable(QString::number(reported)));
    QCOMPARE(reported, handler->duration());
}

void tst_OfonoProvider::startTimeParsed_data()
{
    QTest::addColumn<QString>("startTime");
    QTest::addColumn<QDateTime>("expected");

    QTest::newRow("east") << QStringLiteral("2024-05-01T12:00:00+0300")
                          << QDateTime(QDate(2024, 5, 1), QTime(9, 0), QTimeZone::utc());
    QTest::newRow("west") << QStringLiteral("2024-05-01T12:00:00-0130")
                          << QDateTime(QDate(2024, 5, 1), QTime(13, 30), QTimeZone::utc());
    QTest::newRow("utc") << QStringLiteral("2024-05-01T12:00:00+0000")
                         << QDateTime(QDate(2024, 5, 1), QTime(12, 0), QTimeZone::utc());
    QTest::newRow("empty") << QString() << QDateTime();
    QTest::newRow("garbage") << QStringLiteral("yesterday") << QDateTime();
}

// startedAt() parsed with an empty format and was always invalid.
void tst_OfonoProvider::startTimeParsed()
{
    QFETCH(QString, startTime);
    QFETCH(QDateTime, expected);

    const QDateTime parsed = OfonoVoiceCallHandler::parseStartTime(startTime);
    QCOMPARE(parsed.isValid(), expected.isValid());
    if (expected.isValid())
        QCOMPARE(parsed.toUTC(), expected);

    OfonoVoiceCallHandler *handler = addValidCall(QStringLiteral("/ril_0/voicecall01"),
                                                  QStringLiteral("active"));
    QVERIFY(handler);
    QSignalSpy started(handler, SIGNAL(startedAtChanged(QDateTime)));
    QOfonoVoiceCall::instances().value(QStringLiteral("/ril_0/voicecall01"))->changeStartTime(startTime);
    QCOMPARE(started.count(), 1);
    QCOMPARE(handler->startedAt().isValid(), expected.isValid());
}

// Asynchronous dial failures were never reported.
void tst_OfonoProvider::dialFailureReported()
{
    QSignalSpy providerError(m_provider, SIGNAL(error(QString)));
    QSignalSpy managerError(m_manager, SIGNAL(error(QString)));
    QVERIFY(m_provider->dial(QStringLiteral("12345")));
    QOfonoVoiceCallManager::last()->error = QStringLiteral("org.ofono.Error.InvalidFormat");
    emit QOfonoVoiceCallManager::last()->dialComplete(false);
    QCOMPARE(providerError.count(), 1);
    QCOMPARE(providerError.first().at(0).toString(), QStringLiteral("org.ofono.Error.InvalidFormat"));
    QCOMPARE(managerError.count(), 1);

    emit QOfonoVoiceCallManager::last()->dialComplete(true);
    QCOMPARE(providerError.count(), 1);
}

void tst_OfonoProvider::duplicateCallAddedIgnored()
{
    const QString path = QStringLiteral("/ril_0/voicecall01");
    emit QOfonoVoiceCallManager::last()->callAdded(path);
    QPointer<QOfonoVoiceCall> first = QOfonoVoiceCall::instances().value(path);
    emit QOfonoVoiceCallManager::last()->callAdded(path);
    // Still the same, single call object
    QCOMPARE(QOfonoVoiceCall::instances().value(path), first.data());
    first->becomeValid(QStringLiteral("dialing"));
    QCOMPARE(m_provider->voiceCalls().count(), 1);
}

// A call still waiting for its properties outlived the VoiceCallManager it
// pointed at.
void tst_OfonoProvider::interfaceLossDeletesPendingCalls()
{
    addValidCall(QStringLiteral("/ril_0/voicecall01"), QStringLiteral("active"));
    emit QOfonoVoiceCallManager::last()->callAdded(QStringLiteral("/ril_0/voicecall02"));
    QPointer<QOfonoVoiceCall> pending = QOfonoVoiceCall::instances().value(QStringLiteral("/ril_0/voicecall02"));
    QVERIFY(pending);

    QSignalSpy removed(m_manager, SIGNAL(voiceCallRemoved(QString)));
    QOfonoModem::last()->changeInterfaces(QStringList());
    QCOMPARE(removed.count(), 1);
    QVERIFY(!pending);
    QVERIFY(!QOfonoVoiceCallManager::last());

    // The modem gets the interface back, with the call still pending there
    QOfonoModem::last()->changeInterfaces(QStringList() << QStringLiteral("org.ofono.VoiceCallManager"));
    QVERIFY(QOfonoVoiceCallManager::last());
    QCoreApplication::sendPostedEvents(Q_NULLPTR, QEvent::DeferredDelete);
    QVERIFY(m_provider->voiceCalls().isEmpty());
}

// A call whose properties were already known never got announced.
void tst_OfonoProvider::callValidAtCreationAnnounced()
{
    const QString path = QStringLiteral("/ril_0/voicecall07");
    QOfonoVoiceCall::presetStates().insert(path, QStringLiteral("incoming"));
    QSignalSpy added(m_manager, SIGNAL(voiceCallAdded(AbstractVoiceCallHandler*)));
    emit QOfonoVoiceCallManager::last()->callAdded(path);
    QCOMPARE(added.count(), 1);
    QCOMPARE(m_provider->voiceCalls().count(), 1);
    QVERIFY(m_provider->voiceCalls().first()->isIncoming());
}

// CreateMultiparty joins the modem's active and held calls, so merge()
// asks for it only when the two calls named are those.
void tst_OfonoProvider::mergeJoinsActiveAndHeld()
{
    OfonoVoiceCallHandler *active = addValidCall(QStringLiteral("/ril_0/voicecall01"),
                                                 QStringLiteral("active"));
    OfonoVoiceCallHandler *held = addValidCall(QStringLiteral("/ril_0/voicecall02"),
                                               QStringLiteral("held"));
    QVERIFY(active && held);
    QSignalSpy errors(m_provider, SIGNAL(error(QString)));

    active->merge(held->handlerId());
    QCOMPARE(QOfonoVoiceCallManager::last()->requests, QStringList() << QStringLiteral("createMultiparty"));
    held->merge(active->handlerId());
    QCOMPARE(QOfonoVoiceCallManager::last()->requests.count(QStringLiteral("createMultiparty")), 2);
    QCOMPARE(errors.count(), 0);
}

void tst_OfonoProvider::mergeNeedsActiveAndHeld()
{
    OfonoVoiceCallHandler *first = addValidCall(QStringLiteral("/ril_0/voicecall01"),
                                                QStringLiteral("active"));
    OfonoVoiceCallHandler *second = addValidCall(QStringLiteral("/ril_0/voicecall02"),
                                                 QStringLiteral("dialing"));
    QVERIFY(first && second);
    QSignalSpy errors(m_provider, SIGNAL(error(QString)));

    first->merge(second->handlerId());
    first->merge(first->handlerId());
    first->merge(QStringLiteral("no-such-call"));
    QCOMPARE(errors.count(), 3);
    QVERIFY(errors.at(0).at(0).toString().startsWith(QLatin1String("CreateMultiparty:")));
    QVERIFY(QOfonoVoiceCallManager::last()->requests.isEmpty());
}

void tst_OfonoProvider::mergeFailureReported()
{
    QSignalSpy errors(m_provider, SIGNAL(error(QString)));
    QOfonoVoiceCallManager *manager = QOfonoVoiceCallManager::last();
    manager->error = QStringLiteral("CreateMultiparty: org.ofono.Error.Failed");
    emit manager->createMultipartyComplete(true, QStringList());
    QCOMPARE(errors.count(), 0);
    emit manager->createMultipartyComplete(false, QStringList());
    emit manager->privateChatComplete(false, QStringList());
    QCOMPARE(errors.count(), 2);
    QCOMPARE(errors.at(0).at(0).toString(), manager->error);
}

void tst_OfonoProvider::splitNeedsConference()
{
    const QString path = QStringLiteral("/ril_0/voicecall01");
    OfonoVoiceCallHandler *handler = addValidCall(path, QStringLiteral("active"));
    QVERIFY(handler);
    QSignalSpy errors(m_provider, SIGNAL(error(QString)));

    handler->split();
    QCOMPARE(errors.count(), 1);
    QVERIFY(QOfonoVoiceCallManager::last()->requests.isEmpty());

    QOfonoVoiceCall::instances().value(path)->multipartyCall = true;
    handler->split();
    QCOMPARE(errors.count(), 1);
    QCOMPARE(QOfonoVoiceCallManager::last()->requests, QStringList() << QStringLiteral("privateChat:") + path);
}

// The handlers outlive the call manager they were made with until their
// deferred delete; call control in between must not reach the dead one.
void tst_OfonoProvider::callControlAfterInterfaceLoss()
{
    QPointer<OfonoVoiceCallHandler> active = addValidCall(QStringLiteral("/ril_0/voicecall01"),
                                                          QStringLiteral("active"));
    QPointer<OfonoVoiceCallHandler> held = addValidCall(QStringLiteral("/ril_0/voicecall02"),
                                                        QStringLiteral("held"));
    QVERIFY(active && held);
    const QString heldId = held->handlerId();

    QOfonoModem::last()->changeInterfaces(QStringList());
    QVERIFY(!QOfonoVoiceCallManager::last());
    QSignalSpy errors(m_provider, SIGNAL(error(QString)));
    QVERIFY(active);
    active->merge(heldId);
    active->split();
    active->hold(true);
    active->sendDtmf(QStringLiteral("1"));
    QCOMPARE(errors.count(), 2);
    QCoreApplication::sendPostedEvents(Q_NULLPTR, QEvent::DeferredDelete);
    QVERIFY(!active && !held);
}

// Dual SIM: each modem has its own provider and call manager, and oFono
// can only join calls on one modem.
void tst_OfonoProvider::mergeAcrossModemsRefused()
{
    QOfonoVoiceCallManager *manager0 = QOfonoVoiceCallManager::last();
    OfonoVoiceCallProvider *provider1 = createProvider("/ril_1");
    QOfonoVoiceCallManager *manager1 = QOfonoVoiceCallManager::last();
    QVERIFY(manager0 && manager1 && manager0 != manager1);

    OfonoVoiceCallHandler *active = addValidCall(m_provider, manager0, QStringLiteral("/ril_0/voicecall01"),
                                                 QStringLiteral("active"));
    OfonoVoiceCallHandler *held = addValidCall(provider1, manager1, QStringLiteral("/ril_1/voicecall01"),
                                               QStringLiteral("held"));
    QVERIFY(active && held);
    QVERIFY(active->handlerId() != held->handlerId());
    QSignalSpy errors0(m_provider, SIGNAL(error(QString)));
    QSignalSpy errors1(provider1, SIGNAL(error(QString)));

    active->merge(held->handlerId());
    held->merge(active->handlerId());
    QCOMPARE(errors0.count(), 1);
    QCOMPARE(errors1.count(), 1);
    QVERIFY(manager0->requests.isEmpty());
    QVERIFY(manager1->requests.isEmpty());
    delete provider1;
}

// Dual SIM: one modem losing its voice call interface (SIM removed, modem
// reset) takes only that modem's calls with it.
void tst_OfonoProvider::modemLossLeavesOtherModem()
{
    QOfonoVoiceCallManager *manager0 = QOfonoVoiceCallManager::last();
    QOfonoModem *modem0 = QOfonoModem::last();
    OfonoVoiceCallProvider *provider1 = createProvider("/ril_1");
    QOfonoVoiceCallManager *manager1 = QOfonoVoiceCallManager::last();
    QOfonoModem *modem1 = QOfonoModem::last();
    QVERIFY(modem0 != modem1);

    QPointer<OfonoVoiceCallHandler> call0 = addValidCall(m_provider, manager0, QStringLiteral("/ril_0/voicecall01"),
                                                         QStringLiteral("held"));
    QPointer<OfonoVoiceCallHandler> call1 = addValidCall(provider1, manager1, QStringLiteral("/ril_1/voicecall01"),
                                                         QStringLiteral("active"));
    QVERIFY(call0 && call1);
    QCOMPARE(m_manager->voiceCalls().count(), 2);
    QCOMPARE(m_manager->activeVoiceCall(), static_cast<AbstractVoiceCallHandler *>(call1.data()));

    QSignalSpy removed(m_manager, SIGNAL(voiceCallRemoved(QString)));
    modem1->changeInterfaces(QStringList());
    QCoreApplication::sendPostedEvents(Q_NULLPTR, QEvent::DeferredDelete);
    QCOMPARE(removed.count(), 1);
    QVERIFY(!call1);
    QVERIFY(call0);
    QCOMPARE(m_manager->voiceCalls().count(), 1);
    QCOMPARE(m_manager->activeVoiceCall(), static_cast<AbstractVoiceCallHandler *>(call0.data()));

    call0->hold(false);
    QCOMPARE(manager0->requests, QStringList() << QStringLiteral("swapCalls"));
    delete provider1;
}

static QStringList providerIds(VoiceCallManagerInterface *manager)
{
    QStringList ids;
    foreach (AbstractVoiceCallProvider *provider, manager->providers())
        ids << provider->providerId();
    ids.sort();
    return ids;
}

// voicecall-manager restarted while oFono runs (Restart=always after a
// crash): libqofono reports the modems it finds with modemsChanged() only,
// once its GetModems reply is in. Every one of them needs a provider, or no
// call can be made until a modem comes or goes.
void tst_OfonoProvider::factoryTakesModemsListedLater()
{
    OfonoVoiceCallProviderFactory factory;
    QVERIFY(factory.initialize());
    QVERIFY(factory.configure(m_manager));
    QCOMPARE(providerIds(m_manager), QStringList() << QStringLiteral("ofono-/ril_0"));

    QOfonoManager::last()->getModemsReply(QStringList() << QStringLiteral("/ril_6") << QStringLiteral("/ril_5"));
    QCOMPARE(providerIds(m_manager), QStringList() << QStringLiteral("ofono-/ril_0")
             << QStringLiteral("ofono-/ril_5") << QStringLiteral("ofono-/ril_6"));
}

void tst_OfonoProvider::factoryTakesModemsKnownAtStart()
{
    QOfonoManager::initialModems() = QStringList() << QStringLiteral("/ril_5");
    OfonoVoiceCallProviderFactory factory;
    QVERIFY(factory.initialize());
    QVERIFY(factory.configure(m_manager));
    QCOMPARE(providerIds(m_manager), QStringList() << QStringLiteral("ofono-/ril_0")
             << QStringLiteral("ofono-/ril_5"));
}

// Each modem gets one provider however it is announced, and loses it however
// it goes: removed, missing from a new list, or oFono going away.
void tst_OfonoProvider::factoryFollowsModemChanges()
{
    OfonoVoiceCallProviderFactory factory;
    QVERIFY(factory.initialize());
    QVERIFY(factory.configure(m_manager));
    QOfonoManager *ofono = QOfonoManager::last();
    QSignalSpy changed(m_manager, SIGNAL(providersChanged()));

    ofono->getModemsReply(QStringList() << QStringLiteral("/ril_5"));
    ofono->addModem(QStringLiteral("/ril_6"));
    ofono->getModemsReply(QStringList() << QStringLiteral("/ril_5") << QStringLiteral("/ril_6"));
    QCOMPARE(providerIds(m_manager), QStringList() << QStringLiteral("ofono-/ril_0")
             << QStringLiteral("ofono-/ril_5") << QStringLiteral("ofono-/ril_6"));
    QCOMPARE(changed.count(), 2);

    ofono->removeModem(QStringLiteral("/ril_5"));
    QCOMPARE(providerIds(m_manager), QStringList() << QStringLiteral("ofono-/ril_0")
             << QStringLiteral("ofono-/ril_6"));

    ofono->getModemsReply(QStringList() << QStringLiteral("/ril_7"));
    QCOMPARE(providerIds(m_manager), QStringList() << QStringLiteral("ofono-/ril_0")
             << QStringLiteral("ofono-/ril_7"));

    ofono->ofonoGone();
    QCOMPARE(providerIds(m_manager), QStringList() << QStringLiteral("ofono-/ril_0"));
    QCoreApplication::sendPostedEvents(Q_NULLPTR, QEvent::DeferredDelete);
}

QTEST_GUILESS_MAIN(tst_OfonoProvider)

#include "tst_ofonoprovider.moc"
