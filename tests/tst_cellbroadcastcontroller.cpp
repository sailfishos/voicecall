/*
 * Copyright (C) 2026 Jolla Mobile Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include "cellbroadcastcontroller.h"
#include "cellbroadcastdaemonpolicy_p.h"
#include "cellbroadcasttopics.h"

#include <MDConfItem>
#include <qofonocellbroadcast.h>

#include <QFileInfo>
#include <QScopedPointer>
#include <QtTest>

namespace {

QString catalogPath()
{
    const QString executablePath = QFileInfo(QStringLiteral("/proc/self/exe"))
            .symLinkTarget();
    const QString installedPath = QFileInfo(executablePath).absolutePath()
            + QStringLiteral("/data/test-catalog.json");
    if (QFileInfo(installedPath).isFile()) {
        return installedPath;
    }
    return QFINDTESTDATA("data/test-catalog.json");
}

void configureController(CellBroadcastController *controller, bool alertsEnabled)
{
    controller->setCatalogPath(catalogPath());
    controller->setImsi(QStringLiteral("001010123456789"));
    controller->setSimMcc(QStringLiteral("001"));
    controller->setSimMnc(QStringLiteral("01"));
    controller->setAlertsEnabled(alertsEnabled);
    controller->setModemPath(QStringLiteral("/ril_0"));
}

QString convergeMandatoryTopics(QOfonoCellBroadcast *transport)
{
    if (transport->pendingQueryCount() != 1) {
        return QString();
    }
    transport->finishGetProperties(true, QString());
    if (transport->requestCount() != 1
            || transport->firstRequest().property != QLatin1String("Topics")) {
        return QString();
    }

    const QString managed = transport->firstRequest().value.toString();
    transport->applyFirstRequest();
    transport->finishFirstRequest();
    if (transport->pendingQueryCount() != 1) {
        return QString();
    }
    transport->finishGetProperties(true, managed);
    return transport->requestCount() == 0 ? managed : QString();
}

bool containsTopic(const QString &topics, int topic)
{
    const CellBroadcastTopicRangeList ranges = CellBroadcastTopics::parse(topics);
    for (const CellBroadcastTopicRange &range : ranges) {
        if (topic >= range.from && topic <= range.to) {
            return true;
        }
    }
    return false;
}

} // namespace

class TestCellBroadcastController : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void tracksImplicitStartupQuery();
    void serializesLibqofonoTimeoutRetry();
    void recoversPoweredWriteAfterInterfaceDrop();
    void serializesPoweredBeforeTopics();
    void repowersConvergedTopics();
    void powersWhenEmptyTopicsMatch();
    void removeIntentSurvivesPowerAndFailure();
    void preservesRemovalAcrossInterfaceDrop();
    void removesPossiblyAppliedTopicsAfterInterfaceDrop();
    void resetIntentUsesLatestPolicy();
    void classifiesUsingMessagePlmn();
    void normalizesOneDigitMnc();
    void emergencyAttentionFallback_data();
    void emergencyAttentionFallback();
};

void TestCellBroadcastController::init()
{
    MDConfItem::clear();
    QVERIFY(!QOfonoCellBroadcast::testInstance());
    QVERIFY2(!catalogPath().isEmpty(), "test catalog fixture was not found");
}

void TestCellBroadcastController::tracksImplicitStartupQuery()
{
    QScopedPointer<CellBroadcastController> controller(new CellBroadcastController);
    controller->setCatalogPath(catalogPath());
    controller->setModemPath(QStringLiteral("/ril_0"));
    QOfonoCellBroadcast *transport = QOfonoCellBroadcast::testInstance();
    QVERIFY(transport);

    QCOMPARE(transport->pendingQueryCount(), 1);
    controller->refresh();
    QCOMPARE(transport->pendingQueryCount(), 1);

    transport->finishGetProperties(true, QString());
    QCOMPARE(transport->pendingQueryCount(), 0);
    QCOMPARE(transport->requestCount(), 1);
    QCOMPARE(transport->firstRequest().property, QStringLiteral("Topics"));
}

void TestCellBroadcastController::serializesLibqofonoTimeoutRetry()
{
    QScopedPointer<CellBroadcastController> controller(new CellBroadcastController);
    configureController(controller.data(), false);
    QOfonoCellBroadcast *transport = QOfonoCellBroadcast::testInstance();
    QVERIFY(transport);

    QCOMPARE(transport->pendingQueryCount(), 1);
    transport->finishGetProperties(false, QString(),
                                   QStringLiteral("query timed out"),
                                   QDBusError::Timeout);
    QCOMPARE(transport->pendingQueryCount(), 1);
    QCOMPARE(transport->requestCount(), 0);
    QCOMPARE(controller->errorString(), QString());

    controller->refresh();
    QCOMPARE(transport->pendingQueryCount(), 1);
    transport->finishGetProperties(false, QString());
    QCOMPARE(transport->pendingQueryCount(), 0);
    QCOMPARE(transport->requestCount(), 1);
    QCOMPARE(transport->firstRequest().property, QStringLiteral("Powered"));
}

void TestCellBroadcastController::recoversPoweredWriteAfterInterfaceDrop()
{
    QScopedPointer<CellBroadcastController> controller(new CellBroadcastController);
    configureController(controller.data(), false);
    QOfonoCellBroadcast *transport = QOfonoCellBroadcast::testInstance();
    QVERIFY(transport);

    transport->finishGetProperties(false, QString());
    QCOMPARE(transport->requestCount(), 1);
    QCOMPARE(transport->firstRequest().property, QStringLiteral("Powered"));

    transport->dropInterface();
    QCOMPARE(transport->requestCount(), 0);
    QCOMPARE(transport->pendingQueryCount(), 0);
    QVERIFY(!controller->available());

    transport->reattachInterface();
    QCOMPARE(transport->pendingQueryCount(), 1);
    controller->refresh();
    QCOMPARE(transport->pendingQueryCount(), 1);

    transport->finishGetProperties(false, QString());
    QCOMPARE(transport->requestCount(), 1);
    QCOMPARE(transport->firstRequest().property, QStringLiteral("Powered"));
    transport->applyFirstRequest();
    transport->finishFirstRequest();

    QCOMPARE(transport->pendingQueryCount(), 1);
    transport->finishGetProperties(true, QString());
    QCOMPARE(transport->requestCount(), 1);
    QCOMPARE(transport->firstRequest().property, QStringLiteral("Topics"));
    const QString managed = transport->firstRequest().value.toString();
    transport->applyFirstRequest();
    transport->finishFirstRequest();

    QCOMPARE(transport->pendingQueryCount(), 1);
    transport->finishGetProperties(true, managed);
    QCOMPARE(transport->requestCount(), 0);
}

void TestCellBroadcastController::serializesPoweredBeforeTopics()
{
    QScopedPointer<CellBroadcastController> controller(new CellBroadcastController);
    configureController(controller.data(), false);
    QOfonoCellBroadcast *transport = QOfonoCellBroadcast::testInstance();
    QVERIFY(transport);

    QCOMPARE(transport->pendingQueryCount(), 1);
    transport->finishGetProperties(false, QString());
    QCOMPARE(transport->requestCount(), 1);
    QCOMPARE(transport->firstRequest().property, QStringLiteral("Powered"));
    QCOMPARE(transport->firstRequest().value.toBool(), true);

    // PropertyChanged may arrive before the SetProperty method reply. It must
    // not cause a Topics write to overlap the Powered write.
    transport->applyFirstRequest();
    QCOMPARE(transport->requestCount(), 1);
    transport->finishFirstRequest();
    QCOMPARE(transport->requestCount(), 0);

    QCOMPARE(transport->pendingQueryCount(), 1);
    transport->finishGetProperties(true, QString());
    QCOMPARE(transport->requestCount(), 1);
    QCOMPARE(transport->firstRequest().property, QStringLiteral("Topics"));
    const QString managed = transport->firstRequest().value.toString();
    QVERIFY(containsTopic(managed, 4370));
    QVERIFY(containsTopic(managed, 4383));
    QVERIFY(containsTopic(managed, 4400));

    transport->applyFirstRequest();
    QCOMPARE(transport->requestCount(), 1);
    transport->finishFirstRequest();
    QCOMPARE(transport->pendingQueryCount(), 1);
    transport->finishGetProperties(true, managed);
    QCOMPARE(transport->requestCount(), 0);
}

void TestCellBroadcastController::repowersConvergedTopics()
{
    QScopedPointer<CellBroadcastController> controller(new CellBroadcastController);
    configureController(controller.data(), false);
    QOfonoCellBroadcast *transport = QOfonoCellBroadcast::testInstance();
    QVERIFY(transport);
    const QString managed = convergeMandatoryTopics(transport);
    QVERIFY(!managed.isEmpty());

    transport->changeEnabled(false);
    QCOMPARE(transport->requestCount(), 1);
    QCOMPARE(transport->firstRequest().property, QStringLiteral("Powered"));

    transport->applyFirstRequest();
    transport->finishFirstRequest();
    QCOMPARE(transport->pendingQueryCount(), 1);
    transport->finishGetProperties(true, managed);
    QCOMPARE(transport->requestCount(), 0);
}

void TestCellBroadcastController::powersWhenEmptyTopicsMatch()
{
    QScopedPointer<CellBroadcastController> controller(new CellBroadcastController);
    controller->setCatalogPath(catalogPath());
    controller->setImsi(QStringLiteral("999990123456789"));
    controller->setSimMcc(QStringLiteral("999"));
    controller->setSimMnc(QStringLiteral("99"));
    controller->setAlertsEnabled(false);
    controller->setModemPath(QStringLiteral("/ril_0"));
    QOfonoCellBroadcast *transport = QOfonoCellBroadcast::testInstance();
    QVERIFY(transport);

    QCOMPARE(transport->pendingQueryCount(), 1);
    transport->finishGetProperties(false, QString());
    QCOMPARE(transport->requestCount(), 1);
    QCOMPARE(transport->firstRequest().property, QStringLiteral("Powered"));

    transport->applyFirstRequest();
    transport->finishFirstRequest();
    QCOMPARE(transport->pendingQueryCount(), 1);
    transport->finishGetProperties(true, QString());
    QCOMPARE(transport->requestCount(), 0);
    QCOMPARE(transport->pendingQueryCount(), 0);
}

void TestCellBroadcastController::removeIntentSurvivesPowerAndFailure()
{
    QScopedPointer<CellBroadcastController> controller(new CellBroadcastController);
    configureController(controller.data(), false);
    QOfonoCellBroadcast *transport = QOfonoCellBroadcast::testInstance();
    QVERIFY(transport);
    const QString managed = convergeMandatoryTopics(transport);
    QVERIFY(!managed.isEmpty());

    const QString withUnknown = managed + QStringLiteral(",5000");
    transport->changeTopics(withUnknown);
    QCOMPARE(transport->requestCount(), 0);
    transport->changeEnabled(false);
    QCOMPARE(transport->requestCount(), 1);
    QCOMPARE(transport->firstRequest().property, QStringLiteral("Powered"));

    controller->removeUnknownTopic(QStringLiteral("5000"));
    QCOMPARE(transport->requestCount(), 1);

    transport->applyFirstRequest();
    transport->finishFirstRequest();
    QCOMPARE(transport->pendingQueryCount(), 1);
    transport->finishGetProperties(true, withUnknown);
    QCOMPARE(transport->requestCount(), 1);
    QCOMPARE(transport->firstRequest().property, QStringLiteral("Topics"));
    QVERIFY(!containsTopic(transport->firstRequest().value.toString(), 5000));

    transport->finishFirstRequest(QStringLiteral("write failed"));
    QCOMPARE(transport->requestCount(), 0);
    QCOMPARE(transport->pendingQueryCount(), 0);
    controller->refresh();
    QCOMPARE(transport->pendingQueryCount(), 1);
    transport->finishGetProperties(true, withUnknown);
    QCOMPARE(transport->requestCount(), 1);
    QCOMPARE(transport->firstRequest().property, QStringLiteral("Topics"));
    QVERIFY(!containsTopic(transport->firstRequest().value.toString(), 5000));

    const QString retried = transport->firstRequest().value.toString();
    transport->applyFirstRequest();
    transport->finishFirstRequest();
    QCOMPARE(transport->pendingQueryCount(), 1);
    transport->finishGetProperties(true, retried);
    QCOMPARE(transport->requestCount(), 0);
}

void TestCellBroadcastController::preservesRemovalAcrossInterfaceDrop()
{
    QScopedPointer<CellBroadcastController> controller(new CellBroadcastController);
    configureController(controller.data(), false);
    QOfonoCellBroadcast *transport = QOfonoCellBroadcast::testInstance();
    QVERIFY(transport);
    const QString managed = convergeMandatoryTopics(transport);
    QVERIFY(!managed.isEmpty());

    const QString withUnknown = managed + QStringLiteral(",5000");
    transport->changeTopics(withUnknown);
    controller->removeUnknownTopic(QStringLiteral("5000"));
    QCOMPARE(transport->requestCount(), 1);
    QCOMPARE(transport->firstRequest().property, QStringLiteral("Topics"));

    transport->dropInterface();
    QCOMPARE(transport->requestCount(), 0);
    QCOMPARE(transport->pendingQueryCount(), 0);

    transport->reattachInterface();
    QCOMPARE(transport->pendingQueryCount(), 1);
    controller->refresh();
    QCOMPARE(transport->pendingQueryCount(), 1);
    transport->finishGetProperties(true, withUnknown);

    QCOMPARE(transport->requestCount(), 1);
    QCOMPARE(transport->firstRequest().property, QStringLiteral("Topics"));
    QVERIFY(!containsTopic(transport->firstRequest().value.toString(), 5000));
    const QString retried = transport->firstRequest().value.toString();
    transport->applyFirstRequest();
    transport->finishFirstRequest();

    QCOMPARE(transport->pendingQueryCount(), 1);
    transport->finishGetProperties(true, retried);
    QCOMPARE(transport->requestCount(), 0);
}

void TestCellBroadcastController::removesPossiblyAppliedTopicsAfterInterfaceDrop()
{
    QScopedPointer<CellBroadcastController> controller(new CellBroadcastController);
    configureController(controller.data(), false);
    QOfonoCellBroadcast *transport = QOfonoCellBroadcast::testInstance();
    QVERIFY(transport);
    const QString mandatory = convergeMandatoryTopics(transport);
    QVERIFY(!mandatory.isEmpty());

    controller->setAlertsEnabled(true);
    QCOMPARE(transport->requestCount(), 1);
    QCOMPARE(transport->firstRequest().property, QStringLiteral("Topics"));
    const QString expanded = transport->firstRequest().value.toString();
    QVERIFY(CellBroadcastTopics::parse(expanded).count()
            > CellBroadcastTopics::parse(mandatory).count());

    // The modem applies the write, but the interface drops before the reply.
    transport->applyFirstRequest();
    transport->dropInterface();
    controller->setAlertsEnabled(false);

    transport->reattachInterface();
    QCOMPARE(transport->pendingQueryCount(), 1);
    transport->finishGetProperties(true, expanded);

    QCOMPARE(transport->requestCount(), 1);
    QCOMPARE(transport->firstRequest().property, QStringLiteral("Topics"));
    QVERIFY(CellBroadcastTopics::equals(
                CellBroadcastTopics::parse(transport->firstRequest().value.toString()),
                CellBroadcastTopics::parse(mandatory)));
}

void TestCellBroadcastController::resetIntentUsesLatestPolicy()
{
    QScopedPointer<CellBroadcastController> controller(new CellBroadcastController);
    configureController(controller.data(), false);
    QOfonoCellBroadcast *transport = QOfonoCellBroadcast::testInstance();
    QVERIFY(transport);
    const QString mandatory = convergeMandatoryTopics(transport);
    QVERIFY(!mandatory.isEmpty());

    const QString withUnknown = mandatory + QStringLiteral(",5000");
    transport->changeTopics(withUnknown);
    controller->resetToRecommended();
    QCOMPARE(transport->requestCount(), 1);
    QCOMPARE(transport->firstRequest().property, QStringLiteral("Topics"));
    QVERIFY(CellBroadcastTopics::equals(
                CellBroadcastTopics::parse(transport->firstRequest().value.toString()),
                CellBroadcastTopics::parse(mandatory)));

    controller->setAlertsEnabled(true);
    QCOMPARE(transport->requestCount(), 1);
    transport->applyFirstRequest();
    transport->finishFirstRequest();
    QCOMPARE(transport->pendingQueryCount(), 1);
    transport->finishGetProperties(true, mandatory);

    QCOMPARE(transport->requestCount(), 1);
    QCOMPARE(transport->firstRequest().property, QStringLiteral("Topics"));
    const QString latest = transport->firstRequest().value.toString();
    QVERIFY(containsTopic(latest, 4371));
    QVERIFY(!containsTopic(latest, 5000));
    QVERIFY(CellBroadcastTopics::parse(latest).count()
            > CellBroadcastTopics::parse(mandatory).count());

    transport->applyFirstRequest();
    transport->finishFirstRequest();
    QCOMPARE(transport->pendingQueryCount(), 1);
    transport->finishGetProperties(true, latest);
    QCOMPARE(transport->requestCount(), 0);
}

void TestCellBroadcastController::classifiesUsingMessagePlmn()
{
    QScopedPointer<CellBroadcastController> controller(new CellBroadcastController);
    controller->setCatalogPath(catalogPath());
    controller->setSimMcc(QStringLiteral("999"));
    controller->setSimMnc(QStringLiteral("99"));

    const QVariantMap properties = controller->messagePropertiesForChannel(
                4370, QStringLiteral("001"), QStringLiteral("01"));
    QCOMPARE(properties.value(QStringLiteral("CellBroadcastCategory")).toString(),
             QStringLiteral("critical"));
    QCOMPARE(properties.value(QStringLiteral("CellBroadcastPlmn")).toString(),
             QStringLiteral("00101"));
    QVERIFY(controller->attentionProfileForChannel(
                4370, QStringLiteral("001"), QStringLiteral("01")).isValid());
}

void TestCellBroadcastController::normalizesOneDigitMnc()
{
    QScopedPointer<CellBroadcastController> controller(new CellBroadcastController);
    controller->setCatalogPath(catalogPath());

    const QVariantMap properties = controller->messagePropertiesForChannel(
                4370, QStringLiteral("001"), QStringLiteral("1"));
    QCOMPARE(properties.value(QStringLiteral("CellBroadcastCategory")).toString(),
             QStringLiteral("critical"));
    QCOMPARE(properties.value(QStringLiteral("CellBroadcastPlmn")).toString(),
             QStringLiteral("00101"));
}

void TestCellBroadcastController::emergencyAttentionFallback_data()
{
    QTest::addColumn<bool>("primary");
    QTest::addColumn<bool>("emergencyAlert");
    QTest::addColumn<bool>("attentionAdded");
    QTest::addColumn<bool>("expected");

    QTest::newRow("primary") << true << false << false << true;
    QTest::newRow("emergency-alert") << false << true << false << true;
    QTest::newRow("ordinary") << false << false << false << false;
    QTest::newRow("already-classified") << true << true << true << false;
}

void TestCellBroadcastController::emergencyAttentionFallback()
{
    QFETCH(bool, primary);
    QFETCH(bool, emergencyAlert);
    QFETCH(bool, attentionAdded);
    QFETCH(bool, expected);

    QVariantMap properties;
    properties.insert(QStringLiteral("Primary"), primary);
    properties.insert(QStringLiteral("EmergencyAlert"), emergencyAlert);
    QCOMPARE(cellBroadcastNeedsEmergencyAttention(properties, attentionAdded),
             expected);
}

QTEST_GUILESS_MAIN(TestCellBroadcastController)

#include "tst_cellbroadcastcontroller.moc"
