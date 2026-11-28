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

// The calls on the session bus, as a client on its own connection sees them.

#include "voicecallmanager.h"
#include "voicecallmanagerdbusservice.h"

#include "fake_voicecall.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QtTest>

typedef AbstractVoiceCallHandler H;

static const char CallInterface[] = "org.nemomobile.voicecall.VoiceCall";

class StatusListener : public QObject
{
    Q_OBJECT

public:
    QStringList statuses;

public Q_SLOTS:
    void onStatusChanged(int, const QString &status) { statuses << status; }
};

class tst_DBusService : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void lastStatusReachesClients();
    void removedCallLeavesWithItsHandler();
    void keptHandlerLeavesLater();
    void callBackBeforeItLeftStays();

private:
    static bool registered(const QString &id)
    {
        return QDBusConnection::sessionBus().objectRegisteredAt(QStringLiteral("/calls/") + id);
    }

    QDBusConnection m_client = QDBusConnection(QString());
    VoiceCallManager *m_manager = 0;
    VoiceCallManagerDBusService *m_service = 0;
    FakeVoiceCallProvider *m_provider = 0;
};

void tst_DBusService::initTestCase()
{
    if (!QDBusConnection::sessionBus().isConnected())
        QSKIP("No session bus; run under dbus-run-session");
    m_client = QDBusConnection::connectToBus(QDBusConnection::SessionBus,
                                             QStringLiteral("tst_dbusservice_client"));
    QVERIFY(m_client.isConnected());
}

void tst_DBusService::init()
{
    m_manager = new VoiceCallManager;
    m_service = new VoiceCallManagerDBusService;
    QVERIFY(m_service->initialize());
    // configure() would also take org.nemomobile.voicecall, which the running
    // voicecall-manager owns on a device; the calls need only these two.
    QVERIFY(connect(m_manager, SIGNAL(voiceCallAdded(AbstractVoiceCallHandler*)),
                    m_service, SLOT(onVoiceCallAdded(AbstractVoiceCallHandler*))));
    QVERIFY(connect(m_manager, SIGNAL(voiceCallRemoved(QString)),
                    m_service, SLOT(onVoiceCallRemoved(QString))));
    m_provider = new FakeVoiceCallProvider(QStringLiteral("p1"));
    m_manager->appendProvider(m_provider);
}

void tst_DBusService::cleanup()
{
    m_manager->removeProvider(m_provider);
    delete m_provider;
    m_provider = 0;
    delete m_service;
    m_service = 0;
    delete m_manager;
    m_manager = 0;
    QCoreApplication::sendPostedEvents(Q_NULLPTR, QEvent::DeferredDelete);
}

// A call that ends reports "disconnected" and is removed straight after.
// QtDBus sends an adaptor's signal from its own thread, finding the object's
// path only then: unregistered at removal, the last status was often lost.
// Many calls, as losing it depended on timing.
void tst_DBusService::lastStatusReachesClients()
{
    const QString sender = QDBusConnection::sessionBus().baseService();
    for (int i = 0; i < 50; ++i) {
        const QString id = QStringLiteral("call%1").arg(i);
        const QString path = QStringLiteral("/calls/") + id;
        FakeVoiceCallHandler *call = m_provider->addCall(id, false, H::STATUS_ACTIVE);
        StatusListener listener;
        QVERIFY(m_client.connect(sender, path, QLatin1String(CallInterface),
                                 QStringLiteral("statusChanged"),
                                 &listener, SLOT(onStatusChanged(int,QString))));
        // A round trip on the client connection: the bus has its match rule.
        QVERIFY(m_client.interface()->isServiceRegistered(sender).value());

        call->setStatus(H::STATUS_DISCONNECTED);
        m_provider->removeCall(call);
        QCoreApplication::sendPostedEvents(Q_NULLPTR, QEvent::DeferredDelete);

        QTRY_COMPARE_WITH_TIMEOUT(listener.statuses,
                                  QStringList() << QStringLiteral("disconnected"), 3000);
        m_client.disconnect(sender, path, QLatin1String(CallInterface),
                            QStringLiteral("statusChanged"),
                            &listener, SLOT(onStatusChanged(int,QString)));
    }
}

void tst_DBusService::removedCallLeavesWithItsHandler()
{
    FakeVoiceCallHandler *call = m_provider->addCall(QStringLiteral("a"), false, H::STATUS_ACTIVE);
    QVERIFY(registered(QStringLiteral("a")));
    m_provider->removeCall(call);
    QVERIFY(registered(QStringLiteral("a")));
    QCoreApplication::sendPostedEvents(Q_NULLPTR, QEvent::DeferredDelete);
    QVERIFY(!registered(QStringLiteral("a")));
}

// A provider that goes away takes its calls off the manager without deleting
// them (removeProvider()); the provider may keep them.
void tst_DBusService::keptHandlerLeavesLater()
{
    m_provider->addCall(QStringLiteral("kept"), false, H::STATUS_ACTIVE);
    m_manager->removeProvider(m_provider);
    QVERIFY(registered(QStringLiteral("kept")));
    QTRY_VERIFY_WITH_TIMEOUT(!registered(QStringLiteral("kept")), 5000);
}

// ... and comes back with them before they left (a reconnecting account)
void tst_DBusService::callBackBeforeItLeftStays()
{
    m_provider->addCall(QStringLiteral("back"), false, H::STATUS_HELD);
    m_manager->removeProvider(m_provider);
    m_manager->appendProvider(m_provider);
    QTest::qWait(1500);
    QVERIFY(registered(QStringLiteral("back")));
}

QTEST_GUILESS_MAIN(tst_DBusService)

#include "tst_dbusservice.moc"
