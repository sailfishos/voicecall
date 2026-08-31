/*
 * Copyright (C) 2026 Jolla Mobile Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include "cellbroadcaststore.h"
#include "cellbroadcastcatalog.h"
#include "cellbroadcastgeometry.h"
#ifdef HAVE_CELLBROADCAST_GEOFENCE_TESTS
#include "cellbroadcastgeofence.h"
#endif

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

#ifdef HAVE_CELLBROADCAST_GEOFENCE_TESTS
#include <QGeoCoordinate>
#include <QGeoPositionInfo>
#include <QGeoPositionInfoSource>
#endif

namespace {

QVariantMap alertProperties(int messageIdentifier,
                            int serialNumber,
                            int updateNumber,
                            const QString &languageRole = QStringLiteral("local"),
                            const QString &language = QStringLiteral("en"))
{
    QVariantMap properties;
    properties.insert(QStringLiteral("MessageIdentifier"), messageIdentifier);
    properties.insert(QStringLiteral("Topic"), messageIdentifier);
    properties.insert(QStringLiteral("SerialNumber"), serialNumber);
    properties.insert(QStringLiteral("GeographicalScope"), 1);
    properties.insert(QStringLiteral("MessageCode"), (serialNumber >> 4) & 0x03ff);
    properties.insert(QStringLiteral("UpdateNumber"), updateNumber);
    properties.insert(QStringLiteral("DataCodingScheme"), 0);
    properties.insert(QStringLiteral("Language"), language);
    properties.insert(QStringLiteral("PageCount"), 1);
    properties.insert(QStringLiteral("MobileCountryCode"), QStringLiteral("001"));
    properties.insert(QStringLiteral("MobileNetworkCode"), QStringLiteral("01"));
    properties.insert(QStringLiteral("CellBroadcastPlmn"), QStringLiteral("00101"));
    properties.insert(QStringLiteral("CellBroadcastCategory"), QStringLiteral("critical"));
    properties.insert(QStringLiteral("CellBroadcastTitle"), QStringLiteral("Critical Alert"));
    properties.insert(QStringLiteral("CellBroadcastAlertLevel"), QStringLiteral("1"));
    properties.insert(QStringLiteral("CellBroadcastAttentionPolicy"),
                      QStringLiteral("silent-dnd-override"));
    properties.insert(QStringLiteral("CellBroadcastDisplay"), QStringLiteral("alert"));
    properties.insert(QStringLiteral("CellBroadcastLanguageRole"), languageRole);
    properties.insert(QStringLiteral("CellBroadcastEnabled"), true);
    return properties;
}

QString catalogPath()
{
    const QString overridePath = QString::fromLocal8Bit(
                qgetenv("CELL_BROADCAST_TEST_CATALOG"));
    if (!overridePath.isEmpty()) {
        return overridePath;
    }

    const QString executablePath = QFileInfo(QStringLiteral("/proc/self/exe"))
            .symLinkTarget();
    const QString installedPath = QFileInfo(executablePath).absolutePath()
            + QStringLiteral("/data/test-catalog.json");
    if (QFileInfo(installedPath).isFile()) {
        return installedPath;
    }
    return QFINDTESTDATA("data/test-catalog.json");
}

const CellBroadcastCatalogCategory *categoryById(
        const CellBroadcastCatalogEntry &entry, const QString &id)
{
    for (const CellBroadcastCatalogCategory &category : entry.categories) {
        if (category.id == id) {
            return &category;
        }
    }
    return 0;
}

void verifyLanguagePair(const CellBroadcastCatalogCategory &category,
                        int localChannel, int additionalChannel,
                        bool mandatory)
{
    QCOMPARE(category.ranges.count(), 2);
    QCOMPARE(category.ranges.at(0).from, localChannel);
    QCOMPARE(category.ranges.at(0).to, localChannel);
    QCOMPARE(category.ranges.at(0).languageRole, QStringLiteral("local"));
    QCOMPARE(category.ranges.at(0).mandatory, mandatory);
    QVERIFY(category.ranges.at(0).apply);
    QCOMPARE(category.ranges.at(1).from, additionalChannel);
    QCOMPARE(category.ranges.at(1).to, additionalChannel);
    QCOMPARE(category.ranges.at(1).languageRole, QStringLiteral("additional"));
    QCOMPARE(category.ranges.at(1).mandatory, mandatory);
    QVERIFY(category.ranges.at(1).apply);
}

bool setStoredBootId(const QString &databasePath, const QString &bootId)
{
    static int connectionIndex = 0;
    const QString connectionName = QStringLiteral("cellbroadcast-test-boot-%1")
            .arg(++connectionIndex);
    bool updated = false;
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(
                    QStringLiteral("QSQLITE"), connectionName);
        database.setDatabaseName(databasePath);
        if (database.open()) {
            QSqlQuery query(database);
            query.prepare(QStringLiteral(
                              "UPDATE cellbroadcast_messages SET boot_id=?"));
            query.addBindValue(bootId);
            updated = query.exec();
            database.close();
        }
    }
    QSqlDatabase::removeDatabase(connectionName);
    return updated;
}

bool executeDatabaseStatement(const QString &databasePath, const QString &statement)
{
    static int connectionIndex = 0;
    const QString connectionName = QStringLiteral("cellbroadcast-test-schema-%1")
            .arg(++connectionIndex);
    bool executed = false;
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(
                    QStringLiteral("QSQLITE"), connectionName);
        database.setDatabaseName(databasePath);
        if (database.open()) {
            QSqlQuery query(database);
            executed = query.exec(statement);
            database.close();
        }
    }
    QSqlDatabase::removeDatabase(connectionName);
    return executed;
}

int databaseUserVersion(const QString &databasePath)
{
    static int connectionIndex = 0;
    const QString connectionName = QStringLiteral("cellbroadcast-test-version-%1")
            .arg(++connectionIndex);
    int version = -1;
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(
                    QStringLiteral("QSQLITE"), connectionName);
        database.setDatabaseName(databasePath);
        if (database.open()) {
            QSqlQuery query(database);
            if (query.exec(QStringLiteral("PRAGMA user_version")) && query.next()) {
                version = query.value(0).toInt();
            }
            database.close();
        }
    }
    QSqlDatabase::removeDatabase(connectionName);
    return version;
}

#ifdef HAVE_CELLBROADCAST_GEOFENCE_TESTS
class TestPositionSource : public QGeoPositionInfoSource
{
public:
    explicit TestPositionSource(QObject *parent = 0)
        : QGeoPositionInfoSource(parent)
    {
    }

    QGeoPositionInfo lastKnownPosition(bool = false) const override
    {
        return QGeoPositionInfo();
    }

    PositioningMethods supportedPositioningMethods() const override
    {
        return AllPositioningMethods;
    }

    int minimumUpdateInterval() const override
    {
        return 0;
    }

    Error error() const override
    {
        return NoError;
    }

    void startUpdates() override
    {
    }

    void stopUpdates() override
    {
    }

    void requestUpdate(int = 0) override
    {
    }

    void sendPosition(const QGeoPositionInfo &position)
    {
        Q_EMIT positionUpdated(position);
    }
};
#endif

} // namespace

class TestCellBroadcastStore : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void refusesNonDurableFallback();
    void rejectsUnsupportedSchema();
    void rejectsUnversionedSchema();
    void loadsCategoryPolicy();
    void loadsAttentionProfiles();
    void storesBeforePresentation();
    void queuesIndependentAlerts();
    void suppressesDuplicates();
    void expiresDuplicateIdentities();
    void resetsUpdateOrderingAfterReboot();
    void preservesSameUpdateLanguageAfterReboot();
    void separatesIndependentCategoryChannels();
    void preservesUpdatedVersions();
    void acceptsWrappedHistoricalVersion();
    void groupsLanguageVariants();
    void keepsValidatedGeometryForLanguageVariant();
    void preservesPendingGeoFenceAttentionForLanguageVariant();
    void preservesValidatedPendingGeoFenceAttentionForLanguageVariant();
    void inheritsMissingGeometryForLanguageVariant();
    void rechecksChangedGeometryForLanguageVariant();
    void preservesSharedGeometryForLanguageVariant();
    void presentsMatchingAdditionalLanguageVariant();
    void storesEtwsPrimaryWithoutNetworkText();
    void separatesEtwsServingAreas();
    void waitsForGeoFenceResolution();
    void rechecksNewerGeofencedUpdate();
    void promotesQueueAfterUpdatedGeoFenceIsOutside();
    void resolvesGeoFenceOutside();
    void queuesGeoFenceResultWithoutAttention();
    void findsGeoFenceTriggerReferences();
    void ignoresStaleGeoFenceTriggerReferences();
    void usesLastReceiptForGeoFenceTriggerReferences();
    void isolatesGeoFenceTriggerContext();
    void preparesSharedGeoFenceTrigger();
    void preparesPendingGeoFenceTrigger();
    void preservesRepeatedGeoFenceTriggerDeadline();
    void renewsGeoFenceTriggerDeadlineAfterResolution();
    void resumesGeoFenceChecksAfterRestart();
    void prunesExpiredSuppressedAlerts();
    void retainsUntilUserDeletion();
    void pagesHistoryAndProtectsLiveAlerts();
    void hidesGeofencingTriggers();
    void evaluatesCircleGeometry();
    void evaluatesPolygonGeometry();
#ifdef HAVE_CELLBROADCAST_GEOFENCE_TESTS
    void waitsForAccurateLocation();
    void ignoresStaleLocation();
    void preservesFreshnessForRepeatedGeoFenceRequest();
#endif
};

void TestCellBroadcastStore::loadsCategoryPolicy()
{
    CellBroadcastCatalog catalog;
    const QString path = catalogPath();
    QVERIFY2(!path.isEmpty(), "test catalog fixture was not found");
    QVERIFY2(catalog.load(path), qPrintable(catalog.errorString()));

    const CellBroadcastCatalogEntry entry = catalog.configuredEntryForPlmn(
                QStringLiteral("001"), QStringLiteral("01"));
    QVERIFY(entry.isValid());
    QCOMPARE(entry.alertSystem, QStringLiteral("Test Alerts"));
    QCOMPARE(entry.defaultAttentionProfile, QStringLiteral("standard"));
    QCOMPARE(entry.defaultVibrationProfile, QStringLiteral("wea"));
    QCOMPARE(entry.categories.count(), 7);

    const CellBroadcastCatalogEntry keyed = catalog.entryForKey(QStringLiteral("00101"));
    QCOMPARE(keyed.alertSystem, QStringLiteral("Test Alerts"));

    const CellBroadcastCatalogCategory *critical = categoryById(
                entry, QStringLiteral("critical"));
    QVERIFY(critical);
    QCOMPARE(critical->title, QStringLiteral("Critical Alert"));
    QCOMPARE(critical->alertLevel, QStringLiteral("critical"));
    QCOMPARE(critical->attentionProfile, QStringLiteral("critical"));
    QCOMPARE(critical->attentionPolicy, QStringLiteral("silent-dnd-override"));
    QCOMPARE(critical->display, QStringLiteral("alert"));
    QCOMPARE(critical->sourceRef, QStringLiteral("unit-test-policy"));
    QVERIFY(critical->customName);
    QVERIFY(critical->defaultEnabled);
    QVERIFY(!critical->userConfigurable);
    QVERIFY(critical->settingsVisible);
    verifyLanguagePair(*critical, 4370, 4383, true);

    const CellBroadcastCatalogCategory *priority = categoryById(
                entry, QStringLiteral("priority"));
    QVERIFY(priority);
    QCOMPARE(priority->title, QStringLiteral("Priority Alert"));
    QCOMPARE(priority->alertLevel, QStringLiteral("priority"));
    QCOMPARE(priority->attentionProfile, QStringLiteral("standard"));
    QCOMPARE(priority->attentionPolicy, QStringLiteral("standard"));
    QVERIFY(priority->customName);
    QVERIFY(priority->defaultEnabled);
    QVERIFY(priority->userConfigurable);
    QVERIFY(priority->settingsVisible);
    verifyLanguagePair(*priority, 4371, 4384, false);

    struct OptionalCategory {
        const char *id;
        const char *title;
        const char *level;
        int localChannel;
        int additionalChannel;
        bool defaultEnabled;
    };
    const OptionalCategory optionalCategories[] = {
        { "exercise", "Exercise", "exercise", 4381, 4394, true },
        { "monthly_test", "Test", "test", 4380, 4393, false },
        { "operator_test", "Operator Test", "operator-test", 4382, 4395, false },
        { "state_local_test", "State/Local Test", "state-local-test", 4398, 4399,
          true }
    };
    for (const OptionalCategory &expected : optionalCategories) {
        const CellBroadcastCatalogCategory *category = categoryById(
                    entry, QString::fromLatin1(expected.id));
        QVERIFY(category);
        QCOMPARE(category->title, QString::fromLatin1(expected.title));
        QCOMPARE(category->alertLevel, QString::fromLatin1(expected.level));
        QCOMPARE(category->attentionProfile, QStringLiteral("standard"));
        QCOMPARE(category->attentionPolicy, QStringLiteral("standard"));
        QVERIFY(category->customName);
        QCOMPARE(category->defaultEnabled, expected.defaultEnabled);
        QVERIFY(category->userConfigurable);
        QVERIFY(category->settingsVisible);
        verifyLanguagePair(*category, expected.localChannel,
                           expected.additionalChannel, false);
    }

    const CellBroadcastCatalogCategory *dbgf = categoryById(
                entry, QStringLiteral("geo_fencing"));
    QVERIFY(dbgf);
    QCOMPARE(dbgf->title, QStringLiteral("Device-Based Geo-Fencing"));
    QCOMPARE(dbgf->alertLevel, QStringLiteral("geofencing"));
    QCOMPARE(dbgf->attentionPolicy, QStringLiteral("none"));
    QCOMPARE(dbgf->display, QStringLiteral("none"));
    QVERIFY(dbgf->customName);
    QVERIFY(dbgf->defaultEnabled);
    QVERIFY(!dbgf->userConfigurable);
    QVERIFY(!dbgf->settingsVisible);
    QCOMPARE(dbgf->ranges.count(), 1);
    QCOMPARE(dbgf->ranges.first().from, 4400);
    QCOMPARE(dbgf->ranges.first().to, 4400);
    QVERIFY(dbgf->ranges.first().mandatory);
    QVERIFY(dbgf->ranges.first().apply);
    QCOMPARE(dbgf->ranges.first().languageRole,
             QStringLiteral("not-applicable"));
}

void TestCellBroadcastStore::loadsAttentionProfiles()
{
    CellBroadcastCatalog catalog;
    const QString path = catalogPath();
    QVERIFY2(!path.isEmpty(), "test catalog fixture was not found");
    QVERIFY2(catalog.load(path), qPrintable(catalog.errorString()));

    const CellBroadcastAttentionProfile standard = catalog.attentionProfile(
                QStringLiteral("standard"));
    const CellBroadcastAttentionProfile critical = catalog.attentionProfile(
                QStringLiteral("critical"));
    const CellBroadcastAttentionProfile legacy = catalog.attentionProfile(
                QStringLiteral("legacy"));
    const CellBroadcastVibrationProfile wea = catalog.vibrationProfile(
                QStringLiteral("wea"));
    const CellBroadcastVibrationProfile sos = catalog.vibrationProfile(
                QStringLiteral("sos"));
    QVERIFY(standard.isValid());
    QVERIFY(critical.isValid());
    QVERIFY(legacy.isValid());
    QVERIFY(wea.isValid());
    QVERIFY(sos.isValid());
    QCOMPARE(standard.event, QStringLiteral("cellbroadcast_attention"));
    QCOMPARE(critical.event, QStringLiteral("cellbroadcast_critical_attention"));
    QVERIFY(standard.event != critical.event);
    QCOMPARE(standard.soundFile, critical.soundFile);
    QCOMPARE(critical.soundFile, QStringLiteral(
                 "/usr/share/cell-broadcast-provider-info/attention-tones/"
                 "cellbroadcast-attention-853-960.ogg"));
    QCOMPARE(standard.reservedUse,
             QStringLiteral("official-cell-broadcast-public-warning"));
    QCOMPARE(critical.reservedUse,
             QStringLiteral("official-cell-broadcast-public-warning"));
    QVERIFY(standard.vibrationProfile.isEmpty());
    QCOMPARE(critical.vibrationProfile, QStringLiteral("sos"));
    QVERIFY(standard.vibrationPattern.isEmpty());
    QCOMPARE(critical.vibrationPattern, sos.vibrationPattern);
    QVERIFY(legacy.vibrationProfile.isEmpty());
    QCOMPARE(legacy.vibrationPattern, QList<int>({0, 100, 50, 100}));
    QVERIFY(!legacy.vibrationRepeat);
    QVERIFY(!standard.vibrationRepeat);
    QCOMPARE(critical.vibrationPattern,
             QList<int>({0, 500, 500, 500, 500, 500, 500,
                         1000, 500, 1000, 500, 1000, 500,
                         500, 500, 500, 500, 500, 500}));
    QVERIFY(critical.vibrationRepeat);
    QVERIFY(critical.hapticSequence().endsWith(QStringLiteral(",repeat=forever")));
}

void TestCellBroadcastStore::refusesNonDurableFallback()
{
    CellBroadcastStore store(QStringLiteral("/dev/null/alerts.sqlite"));
    QVERIFY(!store.isOpen());
    QVERIFY(!store.isDurable());
    QVERIFY(!store.store(QStringLiteral("Must not be presented"),
                         alertProperties(4370, 0x1230, 0)).stored);
}

void TestCellBroadcastStore::rejectsUnsupportedSchema()
{
    QTemporaryDir directory;
    const QString path = directory.path() + QStringLiteral("/alerts.sqlite");
    QVERIFY(executeDatabaseStatement(path, QStringLiteral("PRAGMA user_version = 2")));

    CellBroadcastStore store(path);
    QVERIFY(!store.isOpen());
    QCOMPARE(store.errorString(),
             QStringLiteral("Unsupported Cell Broadcast database schema version"));
}

void TestCellBroadcastStore::rejectsUnversionedSchema()
{
    QTemporaryDir directory;
    const QString path = directory.path() + QStringLiteral("/alerts.sqlite");
    QVERIFY(executeDatabaseStatement(
                path, QStringLiteral("CREATE TABLE cellbroadcast_alerts (id INTEGER)")));

    CellBroadcastStore store(path);
    QVERIFY(!store.isOpen());
    QCOMPARE(store.errorString(),
             QStringLiteral("Unversioned Cell Broadcast database schema"));
}

void TestCellBroadcastStore::storesBeforePresentation()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.path() + QStringLiteral("/alerts.sqlite");
    CellBroadcastStore store(path);
    QVERIFY(store.isOpen());
    QVERIFY(store.isDurable());
    QCOMPARE(databaseUserVersion(path), 1);

    const CellBroadcastStore::StoreResult result = store.store(
                QStringLiteral("Take shelter"), alertProperties(4370, 0x1230, 0));
    QVERIFY(result.stored);
    QVERIFY(result.activeChanged);
    QVERIFY(result.presentationChanged);
    QVERIFY(result.requestAttention);
    QCOMPARE(result.activeAlert.value(QStringLiteral("Text")).toString(),
             QStringLiteral("Take shelter"));
    QCOMPARE(store.alertHistory(0, 20).count(), 1);
}

void TestCellBroadcastStore::queuesIndependentAlerts()
{
    QTemporaryDir directory;
    CellBroadcastStore store(directory.path() + QStringLiteral("/alerts.sqlite"));
    const CellBroadcastStore::StoreResult first = store.store(
                QStringLiteral("First"), alertProperties(4370, 0x1230, 0));
    const CellBroadcastStore::StoreResult second = store.store(
                QStringLiteral("Second"), alertProperties(4370, 0x1240, 0));
    QVERIFY(first.activeChanged);
    QVERIFY(first.presentationChanged);
    QVERIFY(!second.activeChanged);
    QVERIFY(second.presentationChanged);
    QVERIFY(!second.requestAttention);
    QCOMPARE(store.activeAlert().value(QStringLiteral("Text")).toString(),
             QStringLiteral("First"));
    QVERIFY(store.acknowledge(first.alertId));
    QCOMPARE(store.activeAlert().value(QStringLiteral("Text")).toString(),
             QStringLiteral("Second"));
}

void TestCellBroadcastStore::suppressesDuplicates()
{
    QTemporaryDir directory;
    CellBroadcastStore store(directory.path() + QStringLiteral("/alerts.sqlite"));
    const QVariantMap properties = alertProperties(4370, 0x1230, 0);
    store.store(QStringLiteral("Duplicate"), properties);
    const CellBroadcastStore::StoreResult duplicate = store.store(
                QStringLiteral("Duplicate"), properties);
    QVERIFY(duplicate.stored);
    QVERIFY(duplicate.duplicate);
    QVERIFY(!duplicate.presentationChanged);
    QVERIFY(!duplicate.requestAttention);
    QCOMPARE(store.alertHistory(0, 20).count(), 1);
    QCOMPARE(store.activeAlert().value(QStringLiteral("ReceiptCount")).toInt(), 2);
}

void TestCellBroadcastStore::expiresDuplicateIdentities()
{
    const qint64 now = QDateTime::currentDateTimeUtc().toMSecsSinceEpoch();
    const QStringList replacementTexts = {
        QStringLiteral("Repeated warning"),
        QStringLiteral("Changed warning")
    };
    for (const QString &replacementText : replacementTexts) {
        QTemporaryDir directory;
        CellBroadcastStore store(directory.path() + QStringLiteral("/alerts.sqlite"));
        QVariantMap oldProperties = alertProperties(4370, 0x1230, 0);
        oldProperties.insert(QStringLiteral("ReceivedAt"), now - 2LL * 24 * 60 * 60 * 1000);
        const CellBroadcastStore::StoreResult old = store.store(
                    QStringLiteral("Repeated warning"), oldProperties);
        QVERIFY(old.stored);
        QVERIFY(store.acknowledge(old.alertId));

        QVariantMap newProperties = alertProperties(4370, 0x1230, 0);
        newProperties.insert(QStringLiteral("ReceivedAt"), now);
        const CellBroadcastStore::StoreResult replacement = store.store(
                    replacementText, newProperties);
        QVERIFY(replacement.stored);
        QVERIFY(!replacement.duplicate);
        QVERIFY(replacement.alertId != old.alertId);
        QVERIFY(replacement.requestAttention);
        QCOMPARE(store.alertHistory(0, 20).count(), 2);
    }
}

void TestCellBroadcastStore::resetsUpdateOrderingAfterReboot()
{
    QTemporaryDir directory;
    const QString path = directory.path() + QStringLiteral("/alerts.sqlite");
    quint64 alertId = 0;
    {
        CellBroadcastStore store(path);
        alertId = store.store(QStringLiteral("Version two"),
                              alertProperties(4370, 0x1232, 2)).alertId;
        const CellBroadcastStore::StoreResult versionNine = store.store(
                    QStringLiteral("Version nine"),
                    alertProperties(4370, 0x1239, 9));
        QCOMPARE(versionNine.alertId, alertId);
        QVERIFY(store.acknowledge(alertId));
    }

    QVERIFY(setStoredBootId(path, QStringLiteral("previous-boot")));
    {
        CellBroadcastStore store(path);
        const CellBroadcastStore::StoreResult currentDuplicate = store.store(
                    QStringLiteral("Version nine"),
                    alertProperties(4370, 0x1239, 9));
        QVERIFY(currentDuplicate.duplicate);
        QVERIFY(!currentDuplicate.requestAttention);
        const CellBroadcastStore::StoreResult repeatedVersion = store.store(
                    QStringLiteral("Version two"),
                    alertProperties(4370, 0x1232, 2));
        QVERIFY(repeatedVersion.stored);
        QVERIFY(!repeatedVersion.duplicate);
        QCOMPARE(repeatedVersion.alertId, alertId);
        QVERIFY(repeatedVersion.requestAttention);
        QCOMPARE(store.activeAlert().value(QStringLiteral("Text")).toString(),
                 QStringLiteral("Version two"));
    }
}

void TestCellBroadcastStore::preservesSameUpdateLanguageAfterReboot()
{
    QTemporaryDir directory;
    const QString path = directory.path() + QStringLiteral("/alerts.sqlite");
    quint64 alertId = 0;
    {
        CellBroadcastStore store(path);
        alertId = store.store(QStringLiteral("Local language"),
                              alertProperties(4370, 0x1239, 9)).alertId;
        QVERIFY(store.acknowledge(alertId));
    }

    QVERIFY(setStoredBootId(path, QStringLiteral("previous-boot")));
    {
        CellBroadcastStore store(path);
        const QVariantMap additional = alertProperties(
                    4383, 0x1239, 9, QStringLiteral("additional"),
                    QLocale::system().name().left(2).toLower());
        const CellBroadcastStore::StoreResult preferred = store.store(
                    QStringLiteral("Preferred language"), additional);
        QVERIFY(preferred.stored);
        QVERIFY(!preferred.requestAttention);
        QVERIFY(store.activeAlert().isEmpty());
        QCOMPARE(store.alert(alertId).value(QStringLiteral("State")).toInt(), 2);

        const CellBroadcastStore::StoreResult wrappedUpdate = store.store(
                    QStringLiteral("Wrapped update"),
                    alertProperties(4370, 0x1232, 2));
        QVERIFY(wrappedUpdate.requestAttention);
        QCOMPARE(store.activeAlert().value(QStringLiteral("Text")).toString(),
                 QStringLiteral("Wrapped update"));
    }
}

void TestCellBroadcastStore::separatesIndependentCategoryChannels()
{
    QTemporaryDir directory;
    CellBroadcastStore store(directory.path() + QStringLiteral("/alerts.sqlite"));
    QVariantMap firstProperties = alertProperties(5000, 0x1230, 0, QString(), QString());
    QVariantMap secondProperties = alertProperties(5001, 0x1230, 0, QString(), QString());
    firstProperties.insert(QStringLiteral("CellBroadcastCategory"), QStringLiteral("additional"));
    secondProperties.insert(QStringLiteral("CellBroadcastCategory"), QStringLiteral("additional"));

    const CellBroadcastStore::StoreResult first = store.store(
                QStringLiteral("First channel"), firstProperties);
    const CellBroadcastStore::StoreResult second = store.store(
                QStringLiteral("Second channel"), secondProperties);
    QVERIFY(first.stored);
    QVERIFY(second.stored);
    QVERIFY(first.alertId != second.alertId);
    QCOMPARE(store.alertHistory(0, 20).count(), 2);
}

void TestCellBroadcastStore::preservesUpdatedVersions()
{
    QTemporaryDir directory;
    CellBroadcastStore store(directory.path() + QStringLiteral("/alerts.sqlite"));
    const CellBroadcastStore::StoreResult original = store.store(
                QStringLiteral("Original"), alertProperties(4370, 0x1230, 0));
    const CellBroadcastStore::StoreResult update = store.store(
                QStringLiteral("Updated"), alertProperties(4370, 0x1231, 1));
    QCOMPARE(update.alertId, original.alertId);
    QVERIFY(update.requestAttention);
    QCOMPARE(store.activeAlert().value(QStringLiteral("Text")).toString(),
             QStringLiteral("Updated"));
    QVERIFY(store.activeAlert().value(QStringLiteral("SupersedesMessageId")).toULongLong());
    QCOMPARE(store.alertHistory(0, 20).count(), 1);
}

void TestCellBroadcastStore::acceptsWrappedHistoricalVersion()
{
    QTemporaryDir directory;
    CellBroadcastStore store(directory.path() + QStringLiteral("/alerts.sqlite"));

    QVariantMap versionZero = alertProperties(4370, 0x1230, 0);
    versionZero.insert(QStringLiteral("Geometries"),
                       QStringLiteral("circle|-35.3,149.1|1000"));
    versionZero.insert(QStringLiteral("WarningAreaCoordinates"),
                       QByteArray::fromHex("010203"));
    const CellBroadcastStore::StoreResult original = store.store(
                QStringLiteral("Repeated body"), versionZero);
    QVERIFY(original.needsGeoCheck);

    QVariantMap versionEight = alertProperties(4370, 0x1238, 8);
    versionEight.insert(QStringLiteral("Geometries"),
                        QStringLiteral("circle|-37.8,144.9|2000"));
    const CellBroadcastStore::StoreResult intermediate = store.store(
                QStringLiteral("Intermediate body"), versionEight);
    QCOMPARE(intermediate.alertId, original.alertId);
    QVERIFY(!intermediate.duplicate);

    versionZero.insert(QStringLiteral("Geometries"),
                       QStringLiteral("circle|-33.9,151.2|3000"));
    versionZero.insert(QStringLiteral("WarningAreaCoordinates"),
                       QByteArray::fromHex("040506"));
    const CellBroadcastStore::StoreResult wrapped = store.store(
                QStringLiteral("Repeated body"), versionZero);
    QVERIFY(wrapped.stored);
    QVERIFY(!wrapped.duplicate);
    QCOMPARE(wrapped.alertId, original.alertId);
    QVERIFY(wrapped.needsGeoCheck);

    const QVariantMap alert = store.alert(original.alertId);
    QCOMPARE(alert.value(QStringLiteral("UpdateNumber")).toInt(), 0);
    QCOMPARE(alert.value(QStringLiteral("Geometries")).toString(),
             QStringLiteral("circle|-33.9,151.2|3000"));
    QCOMPARE(alert.value(QStringLiteral("WarningAreaCoordinates")).toByteArray(),
             QByteArray::fromHex("040506"));
}

void TestCellBroadcastStore::groupsLanguageVariants()
{
    QTemporaryDir directory;
    CellBroadcastStore store(directory.path() + QStringLiteral("/alerts.sqlite"));
    const CellBroadcastStore::StoreResult english = store.store(
                QStringLiteral("English"), alertProperties(4370, 0x1230, 0));
    const QString systemLanguage = QLocale::system().name().left(2).toLower();
    const CellBroadcastStore::StoreResult translated = store.store(
                QStringLiteral("Preferred"),
                alertProperties(4383, 0x1230, 0, QStringLiteral("additional"),
                                systemLanguage));
    QCOMPARE(translated.alertId, english.alertId);
    QVERIFY(!translated.requestAttention);
    QCOMPARE(store.activeAlert().value(QStringLiteral("Text")).toString(),
             QStringLiteral("Preferred"));
    QCOMPARE(store.alertHistory(0, 20).count(), 1);
}

void TestCellBroadcastStore::keepsValidatedGeometryForLanguageVariant()
{
    QTemporaryDir directory;
    CellBroadcastStore store(directory.path() + QStringLiteral("/alerts.sqlite"));
    QVariantMap local = alertProperties(4370, 0x1230, 0);
    local.insert(QStringLiteral("Geometries"),
                 QStringLiteral("circle|-35.3,149.1|1000"));
    const CellBroadcastStore::StoreResult first = store.store(
                QStringLiteral("Local language"), local);
    QVERIFY(first.needsGeoCheck);
    QVERIFY(store.resolveGeoFence(first.alertId, true, QStringLiteral("inside")).stored);

    QVariantMap additional = alertProperties(
                4383, 0x1230, 0, QStringLiteral("additional"),
                QLocale::system().name().left(2).toLower());
    additional.insert(QStringLiteral("Geometries"),
                      QStringLiteral("circle|-35.3,149.1|1000"));
    const CellBroadcastStore::StoreResult preferred = store.store(
                QStringLiteral("Preferred language"), additional);
    QCOMPARE(preferred.alertId, first.alertId);
    QVERIFY(!preferred.needsGeoCheck);
    QVERIFY(!preferred.requestAttention);
    QVERIFY(preferred.activeChanged);
    QCOMPARE(store.activeAlert().value(QStringLiteral("Text")).toString(),
             QStringLiteral("Preferred language"));
    QCOMPARE(store.activeAlert().value(QStringLiteral("GeoState")).toString(),
             QStringLiteral("inside"));
}

void TestCellBroadcastStore::preservesPendingGeoFenceAttentionForLanguageVariant()
{
    QTemporaryDir directory;
    CellBroadcastStore store(directory.path() + QStringLiteral("/alerts.sqlite"));
    QVariantMap local = alertProperties(4370, 0x1230, 0);
    local.insert(QStringLiteral("Geometries"),
                 QStringLiteral("circle|-35.3,149.1|1000"));
    const CellBroadcastStore::StoreResult first = store.store(
                QStringLiteral("Local language"), local);
    QVERIFY(first.needsGeoCheck);

    QVariantMap additional = alertProperties(
                4383, 0x1230, 0, QStringLiteral("additional"),
                QLocale::system().name().left(2).toLower());
    additional.insert(QStringLiteral("Geometries"),
                      QStringLiteral("circle|-35.3,149.1|1000"));
    const CellBroadcastStore::StoreResult preferred = store.store(
                QStringLiteral("Preferred language"), additional);
    QCOMPARE(preferred.alertId, first.alertId);
    QVERIFY(preferred.needsGeoCheck);
    QVERIFY(!preferred.requestAttention);

    const CellBroadcastStore::StoreResult resolved = store.resolveGeoFence(
                first.alertId, true, QStringLiteral("inside"));
    QVERIFY(resolved.requestAttention);
    QCOMPARE(resolved.activeAlert.value(QStringLiteral("Text")).toString(),
             QStringLiteral("Preferred language"));
}

void TestCellBroadcastStore::preservesValidatedPendingGeoFenceAttentionForLanguageVariant()
{
    QTemporaryDir directory;
    CellBroadcastStore store(directory.path() + QStringLiteral("/alerts.sqlite"));
    const CellBroadcastStore::StoreResult active = store.store(
                QStringLiteral("Already active"), alertProperties(4370, 0x2230, 0));
    QVERIFY(active.requestAttention);

    QVariantMap local = alertProperties(4370, 0x1230, 0);
    local.insert(QStringLiteral("Geometries"),
                 QStringLiteral("circle|-35.3,149.1|1000"));
    const CellBroadcastStore::StoreResult queued = store.store(
                QStringLiteral("Local language"), local);
    QVERIFY(queued.needsGeoCheck);
    QVERIFY(store.resolveGeoFence(queued.alertId, true, QStringLiteral("inside")).stored);
    QCOMPARE(store.alert(queued.alertId).value(QStringLiteral("State")).toInt(), 0);

    QVariantMap additional = alertProperties(
                4383, 0x1230, 0, QStringLiteral("additional"),
                QLocale::system().name().left(2).toLower());
    additional.insert(QStringLiteral("Geometries"),
                      QStringLiteral("circle|-35.3,149.1|1000"));
    const CellBroadcastStore::StoreResult preferred = store.store(
                QStringLiteral("Preferred language"), additional);
    QVERIFY(!preferred.needsGeoCheck);
    QCOMPARE(store.alert(queued.alertId).value(QStringLiteral("GeoState")).toString(),
             QStringLiteral("inside"));
    QVERIFY(store.alert(queued.alertId).value(
                QStringLiteral("CellBroadcastGeoFenceAttentionRequired")).toBool());

    const QVariantList prepared = store.prepareGeoFenceTrigger(
                QStringLiteral("4383,4656"), 1,
                QDateTime::currentDateTimeUtc().toMSecsSinceEpoch());
    QCOMPARE(prepared.count(), 1);
    QVERIFY(store.acknowledge(active.alertId));
    QVERIFY(store.activeAlert().isEmpty());

    const CellBroadcastStore::StoreResult resolved = store.resolveGeoFence(
                queued.alertId, true, QStringLiteral("inside"));
    QVERIFY(resolved.requestAttention);
    QCOMPARE(resolved.activeAlert.value(QStringLiteral("Text")).toString(),
             QStringLiteral("Preferred language"));
}

void TestCellBroadcastStore::inheritsMissingGeometryForLanguageVariant()
{
    QTemporaryDir directory;
    CellBroadcastStore store(directory.path() + QStringLiteral("/alerts.sqlite"));
    QVariantMap local = alertProperties(4370, 0x1230, 0);
    local.insert(QStringLiteral("Geometries"),
                 QStringLiteral("circle|-35.3,149.1|1000"));
    local.insert(QStringLiteral("MaximumWaitTime"), 20);
    const CellBroadcastStore::StoreResult first = store.store(
                QStringLiteral("Local language"), local);
    QVERIFY(first.needsGeoCheck);
    const qint64 deadline = store.alert(first.alertId).value(
                QStringLiteral("CellBroadcastGeoFenceDeadline")).toLongLong();

    const QVariantMap additional = alertProperties(
                4383, 0x1230, 0, QStringLiteral("additional"),
                QLocale::system().name().left(2).toLower());
    const CellBroadcastStore::StoreResult preferred = store.store(
                QStringLiteral("Preferred language"), additional);
    QVERIFY(preferred.needsGeoCheck);
    const QVariantMap inherited = store.alert(first.alertId);
    QCOMPARE(inherited.value(QStringLiteral("Geometries")).toString(),
             QStringLiteral("circle|-35.3,149.1|1000"));
    QCOMPARE(inherited.value(QStringLiteral("CellBroadcastGeoFenceDeadline")).toLongLong(),
             deadline);
    QVERIFY(inherited.value(
                QStringLiteral("CellBroadcastGeoFenceAttentionRequired")).toBool());

    const CellBroadcastStore::StoreResult resolved = store.resolveGeoFence(
                first.alertId, true, QStringLiteral("inside"));
    QVERIFY(resolved.requestAttention);
    QCOMPARE(resolved.activeAlert.value(QStringLiteral("Text")).toString(),
             QStringLiteral("Preferred language"));
}

void TestCellBroadcastStore::rechecksChangedGeometryForLanguageVariant()
{
    QTemporaryDir directory;
    CellBroadcastStore store(directory.path() + QStringLiteral("/alerts.sqlite"));
    QVariantMap local = alertProperties(4370, 0x1230, 0);
    local.insert(QStringLiteral("Geometries"),
                 QStringLiteral("circle|-35.3,149.1|1000"));
    const CellBroadcastStore::StoreResult first = store.store(
                QStringLiteral("Local language"), local);
    QVERIFY(store.resolveGeoFence(first.alertId, true,
                                  QStringLiteral("inside")).stored);

    QVariantMap additional = alertProperties(
                4383, 0x1230, 0, QStringLiteral("additional"),
                QLocale::system().name().left(2).toLower());
    additional.insert(QStringLiteral("Geometries"),
                      QStringLiteral("circle|-37.8,144.9|2000"));
    const CellBroadcastStore::StoreResult preferred = store.store(
                QStringLiteral("Preferred language"), additional);
    QVERIFY(preferred.needsGeoCheck);
    QVERIFY(store.activeAlert().isEmpty());
    QCOMPARE(store.alert(first.alertId).value(QStringLiteral("GeoState")).toString(),
             QStringLiteral("checking"));

    const CellBroadcastStore::StoreResult resolved = store.resolveGeoFence(
                first.alertId, true, QStringLiteral("inside"));
    QVERIFY(resolved.stored);
    QVERIFY(!resolved.requestAttention);
    QCOMPARE(store.activeAlert().value(QStringLiteral("Text")).toString(),
             QStringLiteral("Preferred language"));
}

void TestCellBroadcastStore::preservesSharedGeometryForLanguageVariant()
{
    QTemporaryDir directory;
    CellBroadcastStore store(directory.path() + QStringLiteral("/alerts.sqlite"));
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QVariantMap local = alertProperties(4370, 0x1230, 0);
    local.insert(QStringLiteral("Geometries"),
                 QStringLiteral("circle|-35.3,149.1|1000"));
    local.insert(QStringLiteral("MaximumWaitTime"), 10);
    const CellBroadcastStore::StoreResult first = store.store(
                QStringLiteral("Local language"), local);
    QVERIFY(store.resolveGeoFence(first.alertId, false,
                                  QStringLiteral("outside")).stored);

    QVariantMap other = alertProperties(4371, 0x1240, 0);
    other.insert(QStringLiteral("Geometries"),
                 QStringLiteral("circle|-37.8,144.9|2000"));
    other.insert(QStringLiteral("MaximumWaitTime"), 20);
    const CellBroadcastStore::StoreResult second = store.store(
                QStringLiteral("Other warning"), other);
    QVERIFY(store.resolveGeoFence(second.alertId, false,
                                  QStringLiteral("outside")).stored);

    const QVariantList prepared = store.prepareGeoFenceTrigger(
                QStringLiteral("4370,4656;4371,4672"), 2, now);
    QCOMPARE(prepared.count(), 2);

    QVariantMap additional = alertProperties(
                4383, 0x1230, 0, QStringLiteral("additional"),
                QLocale::system().name().left(2).toLower());
    additional.insert(QStringLiteral("Geometries"),
                      QStringLiteral("circle|-35.3,149.1|1000"));
    const CellBroadcastStore::StoreResult preferred = store.store(
                QStringLiteral("Preferred language"), additional);
    QVERIFY(preferred.needsGeoCheck);
    const QVariantMap alert = store.alert(first.alertId);
    const QString shared = alert.value(
                QStringLiteral("CellBroadcastGeoFenceGeometries")).toString();
    QVERIFY(shared.contains(QStringLiteral("circle|-35.3,149.1|1000")));
    QVERIFY(shared.contains(QStringLiteral("circle|-37.8,144.9|2000")));
    QCOMPARE(alert.value(QStringLiteral("CellBroadcastGeoFenceDeadline")).toLongLong(),
             now + 20000);
}

void TestCellBroadcastStore::presentsMatchingAdditionalLanguageVariant()
{
    QTemporaryDir directory;
    CellBroadcastStore store(directory.path() + QStringLiteral("/alerts.sqlite"));
    QVariantMap mismatched = alertProperties(4383, 0x1230, 0,
                                             QStringLiteral("additional"),
                                             QStringLiteral("zz"));
    const CellBroadcastStore::StoreResult suppressed = store.store(
                QStringLiteral("Unsupported language"), mismatched);
    QVERIFY(suppressed.stored);
    QVERIFY(store.activeAlert().isEmpty());

    const QString systemLanguage = QLocale::system().name().left(2).toLower();
    QVariantMap matching = alertProperties(4383, 0x1230, 0,
                                           QStringLiteral("additional"),
                                           systemLanguage);
    const CellBroadcastStore::StoreResult presented = store.store(
                QStringLiteral("Supported language"), matching);
    QCOMPARE(presented.alertId, suppressed.alertId);
    QVERIFY(presented.activeChanged);
    QCOMPARE(store.activeAlert().value(QStringLiteral("Text")).toString(),
             QStringLiteral("Supported language"));
}

void TestCellBroadcastStore::storesEtwsPrimaryWithoutNetworkText()
{
    QTemporaryDir directory;
    CellBroadcastStore store(directory.path() + QStringLiteral("/alerts.sqlite"));
    QVariantMap properties = alertProperties(4352, 0x1230, 0, QString(), QString());
    properties.insert(QStringLiteral("Primary"), true);
    properties.insert(QStringLiteral("EmergencyType"), QStringLiteral("Earthquake"));
    properties.insert(QStringLiteral("EmergencyAlert"), true);

    const CellBroadcastStore::StoreResult result = store.store(QString(), properties);
    QVERIFY(result.stored);
    QVERIFY(result.requestAttention);
    QCOMPARE(result.activeAlert.value(QStringLiteral("Text")).toString(), QString());
    QCOMPARE(result.activeAlert.value(QStringLiteral("EmergencyType")).toString(),
             QStringLiteral("Earthquake"));
}

void TestCellBroadcastStore::separatesEtwsServingAreas()
{
    {
        QTemporaryDir directory;
        CellBroadcastStore store(directory.path() + QStringLiteral("/alerts.sqlite"));
        QVariantMap first = alertProperties(4352, 0x1230, 0);
        first.insert(QStringLiteral("GeographicalScope"), 0);
        first.insert(QStringLiteral("LocationAreaCode"), 10);
        first.insert(QStringLiteral("CellId"), 100);
        first.insert(QStringLiteral("Primary"), true);
        first.insert(QStringLiteral("EmergencyType"), QStringLiteral("Earthquake"));
        first.insert(QStringLiteral("EmergencyAlert"), true);
        QVariantMap second(first);
        second.insert(QStringLiteral("CellId"), 101);

        const CellBroadcastStore::StoreResult firstResult = store.store(QString(), first);
        const CellBroadcastStore::StoreResult secondResult = store.store(QString(), second);
        QVERIFY(firstResult.alertId);
        QVERIFY(secondResult.alertId);
        QVERIFY(firstResult.alertId != secondResult.alertId);
    }

    {
        QTemporaryDir directory;
        CellBroadcastStore store(directory.path() + QStringLiteral("/alerts.sqlite"));
        QVariantMap first = alertProperties(4352, 0x1230, 0);
        first.insert(QStringLiteral("GeographicalScope"), 2);
        first.insert(QStringLiteral("LocationAreaCode"), 10);
        first.insert(QStringLiteral("EmergencyType"), QStringLiteral("Earthquake"));
        first.insert(QStringLiteral("EmergencyAlert"), true);
        QVariantMap second(first);
        second.insert(QStringLiteral("LocationAreaCode"), 11);

        const CellBroadcastStore::StoreResult firstResult = store.store(
                    QStringLiteral("Earthquake warning"), first);
        const CellBroadcastStore::StoreResult secondResult = store.store(
                    QStringLiteral("Earthquake warning"), second);
        QVERIFY(firstResult.alertId);
        QVERIFY(secondResult.alertId);
        QVERIFY(firstResult.alertId != secondResult.alertId);
    }
}

void TestCellBroadcastStore::waitsForGeoFenceResolution()
{
    QTemporaryDir directory;
    CellBroadcastStore store(directory.path() + QStringLiteral("/alerts.sqlite"));
    QVariantMap properties = alertProperties(4370, 0x1230, 0);
    properties.insert(QStringLiteral("Geometries"),
                      QStringLiteral("circle|-35.3,149.1|1000"));
    const CellBroadcastStore::StoreResult stored = store.store(
                QStringLiteral("Local warning"), properties);
    QVERIFY(stored.stored);
    QVERIFY(stored.needsGeoCheck);
    QVERIFY(!stored.presentationChanged);
    QVERIFY(!stored.requestAttention);
    QVERIFY(store.activeAlert().isEmpty());
    QCOMPARE(store.alertHistory(0, 20).count(), 0);

    const CellBroadcastStore::StoreResult resolved = store.resolveGeoFence(
                stored.alertId, true, QStringLiteral("inside"));
    QVERIFY(resolved.presentationChanged);
    QVERIFY(resolved.requestAttention);
    QVERIFY(resolved.activeChanged);
    QCOMPARE(store.activeAlert().value(QStringLiteral("GeoState")).toString(),
             QStringLiteral("inside"));
    QCOMPARE(store.alertHistory(0, 20).count(), 1);
}

void TestCellBroadcastStore::rechecksNewerGeofencedUpdate()
{
    QTemporaryDir directory;
    CellBroadcastStore store(directory.path() + QStringLiteral("/alerts.sqlite"));
    QVariantMap originalProperties = alertProperties(4370, 0x1230, 0);
    originalProperties.insert(QStringLiteral("Geometries"),
                              QStringLiteral("circle|-35.3,149.1|1000"));
    const CellBroadcastStore::StoreResult original = store.store(
                QStringLiteral("Original area"), originalProperties);
    QVERIFY(store.resolveGeoFence(original.alertId, true, QStringLiteral("inside")).stored);
    QVERIFY(store.silence(original.alertId));

    QVariantMap updateProperties = alertProperties(4370, 0x1231, 1);
    updateProperties.insert(QStringLiteral("Geometries"),
                            QStringLiteral("circle|-37.8,144.9|2000"));
    const CellBroadcastStore::StoreResult update = store.store(
                QStringLiteral("Updated area"), updateProperties);
    QCOMPARE(update.alertId, original.alertId);
    QVERIFY(update.needsGeoCheck);
    QVERIFY(!update.requestAttention);
    QVERIFY(update.activeChanged);
    QVERIFY(store.activeAlert().isEmpty());
    QVERIFY(!store.alert(original.alertId).value(QStringLiteral("SilencedAt")).toLongLong());

    const CellBroadcastStore::StoreResult resolved = store.resolveGeoFence(
                original.alertId, true, QStringLiteral("inside"));
    QVERIFY(resolved.requestAttention);
    QVERIFY(resolved.activeChanged);
    QCOMPARE(resolved.activeAlert.value(QStringLiteral("Text")).toString(),
             QStringLiteral("Updated area"));
}

void TestCellBroadcastStore::promotesQueueAfterUpdatedGeoFenceIsOutside()
{
    QTemporaryDir directory;
    CellBroadcastStore store(directory.path() + QStringLiteral("/alerts.sqlite"));
    const CellBroadcastStore::StoreResult first = store.store(
                QStringLiteral("First"), alertProperties(4370, 0x1230, 0));
    const CellBroadcastStore::StoreResult queued = store.store(
                QStringLiteral("Queued"), alertProperties(4370, 0x1240, 0));

    QVariantMap updateProperties = alertProperties(4370, 0x1231, 1);
    updateProperties.insert(QStringLiteral("Geometries"),
                            QStringLiteral("circle|-37.8,144.9|2000"));
    const CellBroadcastStore::StoreResult update = store.store(
                QStringLiteral("Updated first"), updateProperties);
    QCOMPARE(update.alertId, first.alertId);
    QVERIFY(update.needsGeoCheck);
    QVERIFY(store.activeAlert().isEmpty());

    const CellBroadcastStore::StoreResult resolved = store.resolveGeoFence(
                first.alertId, false, QStringLiteral("outside"));
    QVERIFY(resolved.requestAttention);
    QVERIFY(resolved.activeChanged);
    QCOMPARE(resolved.activeAlert.value(QStringLiteral("RecordId")).toULongLong(),
             queued.alertId);
}

void TestCellBroadcastStore::resolvesGeoFenceOutside()
{
    QTemporaryDir directory;
    CellBroadcastStore store(directory.path() + QStringLiteral("/alerts.sqlite"));
    QVariantMap properties = alertProperties(4370, 0x1230, 0);
    properties.insert(QStringLiteral("Geometries"),
                      QStringLiteral("circle|-35.3,149.1|1000"));
    const CellBroadcastStore::StoreResult stored = store.store(
                QStringLiteral("Distant warning"), properties);
    const CellBroadcastStore::StoreResult resolved = store.resolveGeoFence(
                stored.alertId, false, QStringLiteral("outside"));
    QVERIFY(resolved.stored);
    QVERIFY(!resolved.requestAttention);
    QVERIFY(store.activeAlert().isEmpty());
    QCOMPARE(store.alertHistory(0, 20).count(), 0);
}

void TestCellBroadcastStore::queuesGeoFenceResultWithoutAttention()
{
    QTemporaryDir directory;
    CellBroadcastStore store(directory.path() + QStringLiteral("/alerts.sqlite"));
    const CellBroadcastStore::StoreResult active = store.store(
                QStringLiteral("Already active"), alertProperties(4370, 0x1230, 0));
    QVariantMap properties = alertProperties(4370, 0x1240, 0);
    properties.insert(QStringLiteral("Geometries"),
                      QStringLiteral("circle|-35.3,149.1|1000"));
    const CellBroadcastStore::StoreResult waiting = store.store(
                QStringLiteral("Geofenced warning"), properties);

    const CellBroadcastStore::StoreResult resolved = store.resolveGeoFence(
                waiting.alertId, true, QStringLiteral("inside"));
    QVERIFY(resolved.stored);
    QVERIFY(!resolved.requestAttention);
    QVERIFY(!resolved.activeChanged);
    QCOMPARE(store.activeAlert().value(QStringLiteral("RecordId")).toULongLong(),
             active.alertId);
    QVERIFY(store.acknowledge(active.alertId));
    QCOMPARE(store.activeAlert().value(QStringLiteral("RecordId")).toULongLong(),
             waiting.alertId);
}

void TestCellBroadcastStore::findsGeoFenceTriggerReferences()
{
    QTemporaryDir directory;
    CellBroadcastStore store(directory.path() + QStringLiteral("/alerts.sqlite"));
    QVariantMap properties = alertProperties(4370, 0x1230, 0);
    properties.insert(QStringLiteral("Geometries"),
                      QStringLiteral("circle|-35.3,149.1|1000"));
    const CellBroadcastStore::StoreResult stored = store.store(
                QStringLiteral("Referenced warning"), properties);
    const QVariantList alerts = store.geoFenceAlerts(
                QStringLiteral("4370,4656;4371,1"));
    QCOMPARE(alerts.count(), 1);
    QCOMPARE(alerts.first().toMap().value(QStringLiteral("RecordId")).toULongLong(),
             stored.alertId);
}

void TestCellBroadcastStore::ignoresStaleGeoFenceTriggerReferences()
{
    QTemporaryDir directory;
    CellBroadcastStore store(directory.path() + QStringLiteral("/alerts.sqlite"));
    QVariantMap original = alertProperties(4370, 0x1230, 0);
    original.insert(QStringLiteral("Geometries"),
                    QStringLiteral("circle|-35.3,149.1|1000"));
    const CellBroadcastStore::StoreResult first = store.store(
                QStringLiteral("Original warning"), original);
    QVERIFY(store.resolveGeoFence(first.alertId, false,
                                  QStringLiteral("outside")).stored);

    QVariantMap update = alertProperties(4370, 0x1231, 1);
    update.insert(QStringLiteral("Geometries"),
                  QStringLiteral("circle|-37.8,144.9|2000"));
    const CellBroadcastStore::StoreResult newer = store.store(
                QStringLiteral("Updated warning"), update);
    QCOMPARE(newer.alertId, first.alertId);
    QVERIFY(store.resolveGeoFence(first.alertId, false,
                                  QStringLiteral("outside")).stored);

    QVERIFY(store.geoFenceAlerts(QStringLiteral("4370,4656")).isEmpty());
    QCOMPARE(store.geoFenceAlerts(QStringLiteral("4370,4657")).count(), 1);
}

void TestCellBroadcastStore::usesLastReceiptForGeoFenceTriggerReferences()
{
    QTemporaryDir directory;
    CellBroadcastStore store(directory.path() + QStringLiteral("/alerts.sqlite"));
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QVariantMap properties = alertProperties(4370, 0x1230, 0);
    properties.insert(QStringLiteral("Geometries"),
                      QStringLiteral("circle|-35.3,149.1|1000"));
    properties.insert(QStringLiteral("ReceivedAt"),
                      now - 25LL * 60 * 60 * 1000);
    const CellBroadcastStore::StoreResult stored = store.store(
                QStringLiteral("Repeated warning"), properties);
    QVERIFY(store.resolveGeoFence(stored.alertId, false,
                                  QStringLiteral("outside")).stored);

    properties.insert(QStringLiteral("ReceivedAt"),
                      now - 12LL * 60 * 60 * 1000);
    const CellBroadcastStore::StoreResult duplicate = store.store(
                QStringLiteral("Repeated warning"), properties);
    QVERIFY(duplicate.duplicate);
    QCOMPARE(store.geoFenceAlerts(QStringLiteral("4370,4656")).count(), 1);
}

void TestCellBroadcastStore::isolatesGeoFenceTriggerContext()
{
    QTemporaryDir directory;
    CellBroadcastStore store(directory.path() + QStringLiteral("/alerts.sqlite"));
    QVariantMap first = alertProperties(4370, 0x1230, 0);
    first.insert(QStringLiteral("Geometries"),
                 QStringLiteral("circle|-35.3,149.1|1000"));
    first.insert(QStringLiteral("ModemPath"), QStringLiteral("/modem0"));
    const CellBroadcastStore::StoreResult firstResult = store.store(
                QStringLiteral("First network"), first);
    QVERIFY(store.resolveGeoFence(firstResult.alertId, false,
                                  QStringLiteral("outside")).stored);

    QVariantMap second(first);
    second.insert(QStringLiteral("MobileCountryCode"), QStringLiteral("002"));
    second.insert(QStringLiteral("MobileNetworkCode"), QStringLiteral("02"));
    second.insert(QStringLiteral("CellBroadcastPlmn"), QStringLiteral("00202"));
    second.insert(QStringLiteral("ModemPath"), QStringLiteral("/modem1"));
    const CellBroadcastStore::StoreResult secondResult = store.store(
                QStringLiteral("Second network"), second);
    QVERIFY(firstResult.alertId != secondResult.alertId);
    QVERIFY(store.resolveGeoFence(secondResult.alertId, false,
                                  QStringLiteral("outside")).stored);

    QVariantMap trigger = first;
    trigger.remove(QStringLiteral("Geometries"));
    const QVariantList matches = store.geoFenceAlerts(
                QStringLiteral("4370,4656"), trigger);
    QCOMPARE(matches.count(), 1);
    QCOMPARE(matches.first().toMap().value(QStringLiteral("RecordId")).toULongLong(),
             firstResult.alertId);
}

void TestCellBroadcastStore::preparesSharedGeoFenceTrigger()
{
    QTemporaryDir directory;
    const QString path = directory.path() + QStringLiteral("/alerts.sqlite");
    const qint64 receivedAt = QDateTime::currentDateTimeUtc().toMSecsSinceEpoch();
    const qint64 triggerTime = receivedAt + 1000;
    quint64 firstId = 0;
    quint64 secondId = 0;
    {
        CellBroadcastStore store(path);
        QVariantMap first = alertProperties(4370, 0x1230, 0);
        first.insert(QStringLiteral("Geometries"),
                     QStringLiteral("circle|-35.3,149.1|1000"));
        first.insert(QStringLiteral("MaximumWaitTime"), 10);
        first.insert(QStringLiteral("ReceivedAt"), receivedAt);
        firstId = store.store(QStringLiteral("First area"), first).alertId;
        QVERIFY(store.resolveGeoFence(firstId, false, QStringLiteral("outside")).stored);

        QVariantMap second = alertProperties(4371, 0x1240, 0);
        second.insert(QStringLiteral("Geometries"),
                      QStringLiteral("circle|-37.8,144.9|2000"));
        second.insert(QStringLiteral("MaximumWaitTime"), 20);
        second.insert(QStringLiteral("ReceivedAt"), receivedAt);
        secondId = store.store(QStringLiteral("Second area"), second).alertId;
        QVERIFY(store.resolveGeoFence(secondId, false, QStringLiteral("outside")).stored);

        const QVariantList prepared = store.prepareGeoFenceTrigger(
                    QStringLiteral("4370,4656;4371,4672"), 2, triggerTime);
        QCOMPARE(prepared.count(), 2);
        QString sharedGeometries;
        for (const QVariant &value : prepared) {
            const QVariantMap alert = value.toMap();
            const QString geometries = alert.value(
                        QStringLiteral("CellBroadcastGeoFenceGeometries")).toString();
            QVERIFY(geometries.contains(QStringLiteral("circle|-35.3,149.1|1000")));
            QVERIFY(geometries.contains(QStringLiteral("circle|-37.8,144.9|2000")));
            QCOMPARE(alert.value(QStringLiteral("CellBroadcastGeoFenceDeadline")).toLongLong(),
                     triggerTime + 20000);
            QCOMPARE(alert.value(QStringLiteral("State")).toInt(), 4);
            sharedGeometries = geometries;
        }
        QCOMPARE(CellBroadcastGeometry::evaluate(sharedGeometries, -35.3, 149.1, 10),
                 CellBroadcastGeometry::Inside);
        QCOMPARE(CellBroadcastGeometry::evaluate(sharedGeometries, -37.8, 144.9, 10),
                 CellBroadcastGeometry::Inside);
        QCOMPARE(CellBroadcastGeometry::evaluate(sharedGeometries, -30.0, 140.0, 10),
                 CellBroadcastGeometry::Outside);
    }
    {
        CellBroadcastStore store(path);
        const QVariantList pending = store.pendingGeoFenceAlerts();
        QCOMPARE(pending.count(), 2);
        QSet<quint64> ids;
        for (const QVariant &value : pending) {
            const QVariantMap alert = value.toMap();
            ids.insert(alert.value(QStringLiteral("RecordId")).toULongLong());
            QCOMPARE(alert.value(QStringLiteral("CellBroadcastGeoFenceDeadline")).toLongLong(),
                     triggerTime + 20000);
        }
        QVERIFY(ids.contains(firstId));
        QVERIFY(ids.contains(secondId));
    }
}

void TestCellBroadcastStore::preparesPendingGeoFenceTrigger()
{
    QTemporaryDir directory;
    CellBroadcastStore store(directory.path() + QStringLiteral("/alerts.sqlite"));
    const CellBroadcastStore::StoreResult active = store.store(
                QStringLiteral("Already active"), alertProperties(4370, 0x1230, 0));
    QVERIFY(active.stored);

    const qint64 receivedAt = QDateTime::currentDateTimeUtc().toMSecsSinceEpoch();
    QVariantMap queuedProperties = alertProperties(4371, 0x1240, 0);
    queuedProperties.insert(QStringLiteral("Geometries"),
                            QStringLiteral("circle|-35.3,149.1|1000"));
    queuedProperties.insert(QStringLiteral("MaximumWaitTime"), 10);
    queuedProperties.insert(QStringLiteral("ReceivedAt"), receivedAt);
    const CellBroadcastStore::StoreResult queued = store.store(
                QStringLiteral("Queued warning"), queuedProperties);
    QVERIFY(queued.needsGeoCheck);
    const CellBroadcastStore::StoreResult queuedResolved = store.resolveGeoFence(
                queued.alertId, true, QStringLiteral("inside"));
    QVERIFY(queuedResolved.stored);
    QVERIFY(queuedResolved.presentationChanged);
    QVERIFY(!queuedResolved.activeChanged);
    QCOMPARE(store.alert(queued.alertId).value(QStringLiteral("State")).toInt(), 0);

    QVariantMap suppressedProperties = alertProperties(4372, 0x1250, 0);
    suppressedProperties.insert(QStringLiteral("Geometries"),
                                QStringLiteral("circle|-37.8,144.9|2000"));
    suppressedProperties.insert(QStringLiteral("MaximumWaitTime"), 20);
    suppressedProperties.insert(QStringLiteral("ReceivedAt"), receivedAt);
    const CellBroadcastStore::StoreResult suppressed = store.store(
                QStringLiteral("Suppressed warning"), suppressedProperties);
    QVERIFY(suppressed.needsGeoCheck);
    const CellBroadcastStore::StoreResult suppressedResolved = store.resolveGeoFence(
                suppressed.alertId, false, QStringLiteral("outside"));
    QVERIFY(suppressedResolved.stored);
    QVERIFY(!suppressedResolved.presentationChanged);

    const qint64 triggerTime = receivedAt + 1000;
    const QVariantList prepared = store.prepareGeoFenceTrigger(
                QStringLiteral("4371,4672;4372,4688"), 2, triggerTime);
    QCOMPARE(prepared.count(), 2);
    for (const QVariant &value : prepared) {
        const QVariantMap alert = value.toMap();
        QCOMPARE(alert.value(QStringLiteral("State")).toInt(), 4);
        const QString geometries = alert.value(
                    QStringLiteral("CellBroadcastGeoFenceGeometries")).toString();
        QVERIFY(geometries.contains(QStringLiteral("circle|-35.3,149.1|1000")));
        QVERIFY(geometries.contains(QStringLiteral("circle|-37.8,144.9|2000")));
        QCOMPARE(alert.value(QStringLiteral("CellBroadcastGeoFenceDeadline")).toLongLong(),
                 triggerTime + 20000);
    }
    QCOMPARE(store.activeAlert().value(QStringLiteral("RecordId")).toULongLong(),
             active.alertId);
}

void TestCellBroadcastStore::preservesRepeatedGeoFenceTriggerDeadline()
{
    QTemporaryDir directory;
    CellBroadcastStore store(directory.path() + QStringLiteral("/alerts.sqlite"));
    const qint64 receivedAt = QDateTime::currentDateTimeUtc().toMSecsSinceEpoch();
    QVariantMap properties = alertProperties(4370, 0x1230, 0);
    properties.insert(QStringLiteral("Geometries"),
                      QStringLiteral("circle|-35.3,149.1|1000"));
    properties.insert(QStringLiteral("MaximumWaitTime"), 20);
    properties.insert(QStringLiteral("ReceivedAt"), receivedAt);
    const CellBroadcastStore::StoreResult stored = store.store(
                QStringLiteral("Repeated trigger"), properties);
    QVERIFY(store.resolveGeoFence(stored.alertId, false,
                                  QStringLiteral("outside")).stored);

    const qint64 firstTriggerTime = receivedAt + 1000;
    const QVariantList first = store.prepareGeoFenceTrigger(
                QStringLiteral("4370,4656"), 1, firstTriggerTime);
    QCOMPARE(first.count(), 1);
    const qint64 firstDeadline = firstTriggerTime + 20000;
    QCOMPARE(first.first().toMap().value(
                 QStringLiteral("CellBroadcastGeoFenceDeadline")).toLongLong(),
             firstDeadline);

    const QVariantList repeated = store.prepareGeoFenceTrigger(
                QStringLiteral("4370,4656"), 1, receivedAt + 5000);
    QCOMPARE(repeated.count(), 1);
    QCOMPARE(repeated.first().toMap().value(QStringLiteral("State")).toInt(), 4);
    QCOMPARE(repeated.first().toMap().value(
                 QStringLiteral("CellBroadcastGeoFenceDeadline")).toLongLong(),
             firstDeadline);
}

void TestCellBroadcastStore::renewsGeoFenceTriggerDeadlineAfterResolution()
{
    QTemporaryDir directory;
    CellBroadcastStore store(directory.path() + QStringLiteral("/alerts.sqlite"));
    const qint64 receivedAt = QDateTime::currentDateTimeUtc().toMSecsSinceEpoch();
    QVariantMap properties = alertProperties(4370, 0x1230, 0);
    properties.insert(QStringLiteral("Geometries"),
                      QStringLiteral("circle|-35.3,149.1|1000"));
    properties.insert(QStringLiteral("MaximumWaitTime"), 20);
    properties.insert(QStringLiteral("ReceivedAt"), receivedAt);
    const CellBroadcastStore::StoreResult stored = store.store(
                QStringLiteral("Resolved trigger"), properties);
    QVERIFY(store.resolveGeoFence(stored.alertId, false,
                                  QStringLiteral("outside")).stored);

    const QVariantList first = store.prepareGeoFenceTrigger(
                QStringLiteral("4370,4656"), 1, receivedAt + 1000);
    QCOMPARE(first.count(), 1);
    QVERIFY(store.resolveGeoFence(stored.alertId, false,
                                  QStringLiteral("outside")).stored);

    const qint64 secondTriggerTime = receivedAt + 5000;
    const QVariantList fresh = store.prepareGeoFenceTrigger(
                QStringLiteral("4370,4656"), 1, secondTriggerTime);
    QCOMPARE(fresh.count(), 1);
    QCOMPARE(fresh.first().toMap().value(QStringLiteral("State")).toInt(), 4);
    QCOMPARE(fresh.first().toMap().value(
                 QStringLiteral("CellBroadcastGeoFenceDeadline")).toLongLong(),
             secondTriggerTime + 20000);
}

void TestCellBroadcastStore::resumesGeoFenceChecksAfterRestart()
{
    QTemporaryDir directory;
    const QString path = directory.path() + QStringLiteral("/alerts.sqlite");
    quint64 alertId = 0;
    {
        CellBroadcastStore store(path);
        QVariantMap properties = alertProperties(4370, 0x1230, 0);
        properties.insert(QStringLiteral("Geometries"),
                          QStringLiteral("circle|-35.3,149.1|1000"));
        properties.insert(QStringLiteral("MaximumWaitTime"), 20);
        properties.insert(QStringLiteral("ReceivedAt"),
                          QDateTime::currentDateTimeUtc().toMSecsSinceEpoch() - 5000);
        alertId = store.store(QStringLiteral("Pending location"), properties).alertId;
        const QVariantMap alert = store.alert(alertId);
        QCOMPARE(alert.value(QStringLiteral("CellBroadcastGeoFenceDeadline")).toLongLong(),
                 properties.value(QStringLiteral("ReceivedAt")).toLongLong() + 20000);
    }
    {
        CellBroadcastStore store(path);
        const QVariantList pending = store.pendingGeoFenceAlerts();
        QCOMPARE(pending.count(), 1);
        QCOMPARE(pending.first().toMap().value(QStringLiteral("RecordId")).toULongLong(),
                 alertId);
        QCOMPARE(pending.first().toMap().value(
                     QStringLiteral("CellBroadcastGeoFenceDeadline")).toLongLong(),
                 pending.first().toMap().value(QStringLiteral("ReceivedAt")).toLongLong()
                 + 20000);
    }
}

void TestCellBroadcastStore::retainsUntilUserDeletion()
{
    QTemporaryDir directory;
    const QString path = directory.path() + QStringLiteral("/alerts.sqlite");
    quint64 alertId = 0;
    {
        CellBroadcastStore store(path);
        const CellBroadcastStore::StoreResult result = store.store(
                    QStringLiteral("Persistent"), alertProperties(4370, 0x1230, 0));
        alertId = result.alertId;
        QVERIFY(store.acknowledge(alertId));
    }
    {
        CellBroadcastStore store(path);
        QCOMPARE(store.alertHistory(0, 20).count(), 1);
        QVERIFY(store.remove(alertId));
        QCOMPARE(store.alertHistory(0, 20).count(), 0);
        const CellBroadcastStore::StoreResult replacement = store.store(
                    QStringLiteral("Persistent"), alertProperties(4370, 0x1230, 0));
        QVERIFY(replacement.stored);
        QVERIFY(!replacement.duplicate);
        QVERIFY(replacement.alertId != alertId);
    }
}

void TestCellBroadcastStore::prunesExpiredSuppressedAlerts()
{
    QTemporaryDir directory;
    const QString path = directory.path() + QStringLiteral("/alerts.sqlite");
    const qint64 now = QDateTime::currentDateTimeUtc().toMSecsSinceEpoch();
    const qint64 hour = 60LL * 60 * 1000;
    quint64 refreshedId = 0;
    quint64 lateRefreshedId = 0;
    quint64 acknowledgedId = 0;
    quint64 startupExpiredId = 0;

    {
        CellBroadcastStore store(path);

        QVariantMap refreshed = alertProperties(4400, 0x1230, 0);
        refreshed.insert(QStringLiteral("CellBroadcastDisplay"),
                         QStringLiteral("none"));
        refreshed.insert(QStringLiteral("ReceivedAt"), now - 23 * hour);
        refreshedId = store.store(QStringLiteral("Refreshed hidden alert"),
                                  refreshed).alertId;
        refreshed.insert(QStringLiteral("ReceivedAt"), now);
        const CellBroadcastStore::StoreResult duplicate = store.store(
                    QStringLiteral("Refreshed hidden alert"), refreshed);
        QVERIFY(duplicate.duplicate);
        QCOMPARE(duplicate.alertId, refreshedId);

        QVariantMap lateRefreshed = alertProperties(4400, 0x1280, 0);
        lateRefreshed.insert(QStringLiteral("CellBroadcastDisplay"),
                             QStringLiteral("none"));
        lateRefreshed.insert(QStringLiteral("ReceivedAt"), now - 25 * hour);
        lateRefreshedId = store.store(QStringLiteral("Late-refreshed hidden alert"),
                                      lateRefreshed).alertId;
        lateRefreshed.insert(QStringLiteral("ReceivedAt"), now - 12 * hour);
        const CellBroadcastStore::StoreResult lateDuplicate = store.store(
                    QStringLiteral("Late-refreshed hidden alert"), lateRefreshed);
        QVERIFY(lateDuplicate.duplicate);
        QCOMPARE(lateDuplicate.alertId, lateRefreshedId);
        QCOMPARE(store.alert(lateRefreshedId).value(
                     QStringLiteral("LastReceivedAt")).toLongLong(),
                 now - 12 * hour);

        QVariantMap acknowledged = alertProperties(4370, 0x1240, 0);
        acknowledged.insert(QStringLiteral("ReceivedAt"), now - 25 * hour);
        acknowledgedId = store.store(QStringLiteral("Archived alert"),
                                     acknowledged).alertId;
        QVERIFY(store.acknowledge(acknowledgedId));

        QVariantMap expired = alertProperties(4400, 0x1250, 0);
        expired.insert(QStringLiteral("CellBroadcastDisplay"),
                       QStringLiteral("none"));
        expired.insert(QStringLiteral("ReceivedAt"), now - 25 * hour);
        const quint64 expiredId = store.store(QStringLiteral("Expired hidden alert"),
                                              expired).alertId;
        QVERIFY(!store.alert(expiredId).isEmpty());

        QVariantMap recent = alertProperties(4400, 0x1260, 0);
        recent.insert(QStringLiteral("CellBroadcastDisplay"),
                      QStringLiteral("none"));
        recent.insert(QStringLiteral("ReceivedAt"), now);
        QVERIFY(store.store(QStringLiteral("Recent hidden alert"), recent).stored);
        QVERIFY(store.alert(expiredId).isEmpty());
        QVERIFY(!store.alert(refreshedId).isEmpty());
        QVERIFY(!store.alert(lateRefreshedId).isEmpty());
        QVERIFY(!store.alert(acknowledgedId).isEmpty());

        QVariantMap startupExpired = alertProperties(4400, 0x1270, 0);
        startupExpired.insert(QStringLiteral("CellBroadcastDisplay"),
                              QStringLiteral("none"));
        startupExpired.insert(QStringLiteral("ReceivedAt"), now - 25 * hour);
        startupExpiredId = store.store(QStringLiteral("Startup-expired hidden alert"),
                                       startupExpired).alertId;
        QVERIFY(!store.alert(startupExpiredId).isEmpty());
    }

    CellBroadcastStore reopened(path);
    QVERIFY2(reopened.isOpen(), qPrintable(reopened.errorString()));
    QVERIFY(reopened.alert(startupExpiredId).isEmpty());
    QVERIFY(!reopened.alert(refreshedId).isEmpty());
    QVERIFY(!reopened.alert(lateRefreshedId).isEmpty());
    QVERIFY(!reopened.alert(acknowledgedId).isEmpty());
}

void TestCellBroadcastStore::pagesHistoryAndProtectsLiveAlerts()
{
    QTemporaryDir directory;
    CellBroadcastStore store(directory.path() + QStringLiteral("/alerts.sqlite"));
    QList<quint64> alertIds;

    for (int index = 0; index < 55; ++index) {
        const CellBroadcastStore::StoreResult result = store.store(
                    QStringLiteral("Archived alert %1").arg(index),
                    alertProperties(4370, 0x1000 + (index << 4), 0));
        QVERIFY(result.stored);
        alertIds.append(result.alertId);
    }

    QVERIFY(!store.remove(alertIds.at(0)));
    QVERIFY(!store.remove(alertIds.at(1)));

    while (!store.activeAlert().isEmpty()) {
        QVERIFY(store.acknowledge(store.activeAlert()
                                  .value(QStringLiteral("RecordId"))
                                  .toULongLong()));
    }

    const QVariantList firstPage = store.alertHistory(0, 50);
    QCOMPARE(firstPage.count(), 50);
    QCOMPARE(firstPage.first().toMap().value(QStringLiteral("RecordId")).toULongLong(),
             alertIds.last());
    const quint64 beforeId = firstPage.last().toMap()
            .value(QStringLiteral("RecordId")).toULongLong();
    const QVariantList secondPage = store.alertHistory(beforeId, 50);
    QCOMPARE(secondPage.count(), 5);
    QCOMPARE(secondPage.first().toMap().value(QStringLiteral("RecordId")).toULongLong(),
             alertIds.at(4));
    QCOMPARE(secondPage.last().toMap().value(QStringLiteral("RecordId")).toULongLong(),
             alertIds.first());
    QCOMPARE(store.alertHistory(0, 500).count(), 55);
    QCOMPARE(store.alertHistory(0, 0).count(), 1);

    QVERIFY(store.remove(alertIds.first()));
    QVERIFY(!store.remove(alertIds.first()));
    QCOMPARE(store.clearHistory(), 54);
    QVERIFY(store.alertHistory(0, 50).isEmpty());
}

void TestCellBroadcastStore::hidesGeofencingTriggers()
{
    QTemporaryDir directory;
    CellBroadcastStore store(directory.path() + QStringLiteral("/alerts.sqlite"));
    QVariantMap properties = alertProperties(4400, 0x1230, 0);
    properties.insert(QStringLiteral("CellBroadcastCategory"), QStringLiteral("geo_fencing"));
    properties.insert(QStringLiteral("CellBroadcastDisplay"), QStringLiteral("none"));
    const CellBroadcastStore::StoreResult result = store.store(
                QStringLiteral("DBGF"), properties);
    QVERIFY(result.stored);
    QVERIFY(!result.requestAttention);
    QVERIFY(store.activeAlert().isEmpty());
    QCOMPARE(store.alertHistory(0, 20).count(), 0);
}

void TestCellBroadcastStore::evaluatesCircleGeometry()
{
    const QString geometry = QStringLiteral("circle|-35.2809,149.1300|1000");
    QCOMPARE(CellBroadcastGeometry::evaluate(geometry, -35.2809, 149.1300, 10),
             CellBroadcastGeometry::Inside);
    QCOMPARE(CellBroadcastGeometry::evaluate(geometry, -35.2809, 149.1425, 300),
             CellBroadcastGeometry::Ambiguous);
    QCOMPARE(CellBroadcastGeometry::evaluate(geometry, -35.2809, 149.1600, 10),
             CellBroadcastGeometry::Outside);
}

void TestCellBroadcastStore::evaluatesPolygonGeometry()
{
    const QString geometry = QStringLiteral(
                "polygon|-35.30,149.10|-35.30,149.20|-35.20,149.20|-35.20,149.10");
    QCOMPARE(CellBroadcastGeometry::evaluate(geometry, -35.25, 149.15, 10),
             CellBroadcastGeometry::Inside);
    QCOMPARE(CellBroadcastGeometry::evaluate(geometry, -35.301, 149.15, 200),
             CellBroadcastGeometry::Ambiguous);
    QCOMPARE(CellBroadcastGeometry::evaluate(geometry, -35.40, 149.15, 10),
             CellBroadcastGeometry::Outside);
    QCOMPARE(CellBroadcastGeometry::evaluate(QStringLiteral("polygon|bad"),
                                              -35.25, 149.15, 10),
             CellBroadcastGeometry::Invalid);
}

#ifdef HAVE_CELLBROADCAST_GEOFENCE_TESTS
void TestCellBroadcastStore::waitsForAccurateLocation()
{
    TestPositionSource source;
    CellBroadcastGeoFence geoFence(0, &source);
    QSignalSpy resolved(&geoFence, &CellBroadcastGeoFence::resolved);
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    geoFence.check(1, QStringLiteral("circle|-35.3,149.1|1000"),
                   now + 10000, now);

    QGeoPositionInfo missingAccuracy(QGeoCoordinate(-30.0, 140.0),
                                     QDateTime::currentDateTimeUtc());
    source.sendPosition(missingAccuracy);
    QCOMPARE(resolved.count(), 0);

    QGeoPositionInfo accurateOutside(QGeoCoordinate(-30.0, 140.0),
                                     QDateTime::currentDateTimeUtc());
    accurateOutside.setAttribute(QGeoPositionInfo::HorizontalAccuracy, 10.0);
    source.sendPosition(accurateOutside);
    QCOMPARE(resolved.count(), 1);
    QCOMPARE(resolved.takeFirst().at(1).toBool(), false);
}

void TestCellBroadcastStore::ignoresStaleLocation()
{
    TestPositionSource source;
    CellBroadcastGeoFence geoFence(0, &source);
    QSignalSpy resolved(&geoFence, &CellBroadcastGeoFence::resolved);
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    geoFence.check(1, QStringLiteral("circle|-35.3,149.1|1000"),
                   now + 10000, now);

    QGeoPositionInfo staleOutside(
                QGeoCoordinate(-30.0, 140.0),
                QDateTime::fromMSecsSinceEpoch(now - 1000, Qt::UTC));
    staleOutside.setAttribute(QGeoPositionInfo::HorizontalAccuracy, 10.0);
    source.sendPosition(staleOutside);
    QCOMPARE(resolved.count(), 0);

    QGeoPositionInfo freshOutside(
                QGeoCoordinate(-30.0, 140.0),
                QDateTime::fromMSecsSinceEpoch(
                    QDateTime::currentMSecsSinceEpoch() + 1, Qt::UTC));
    freshOutside.setAttribute(QGeoPositionInfo::HorizontalAccuracy, 10.0);
    source.sendPosition(freshOutside);
    QCOMPARE(resolved.count(), 1);
    QCOMPARE(resolved.takeFirst().at(1).toBool(), false);
}

void TestCellBroadcastStore::preservesFreshnessForRepeatedGeoFenceRequest()
{
    TestPositionSource source;
    CellBroadcastGeoFence geoFence(0, &source);
    QSignalSpy resolved(&geoFence, &CellBroadcastGeoFence::resolved);
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const QString geometry = QStringLiteral("circle|-35.3,149.1|1000");
    const qint64 deadline = now + 10000;
    geoFence.check(1, geometry, deadline, now);
    const qint64 positionTime = QDateTime::currentMSecsSinceEpoch();

    QTest::qWait(20);
    geoFence.check(1, geometry, deadline, now);

    QGeoPositionInfo freshOutside(
                QGeoCoordinate(-30.0, 140.0),
                QDateTime::fromMSecsSinceEpoch(positionTime, Qt::UTC));
    freshOutside.setAttribute(QGeoPositionInfo::HorizontalAccuracy, 10.0);
    source.sendPosition(freshOutside);
    QCOMPARE(resolved.count(), 1);
    QCOMPARE(resolved.takeFirst().at(1).toBool(), false);
}
#endif

QTEST_APPLESS_MAIN(TestCellBroadcastStore)
#include "tst_cellbroadcaststore.moc"
