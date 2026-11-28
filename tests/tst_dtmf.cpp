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

// DTMF key to Telepathy event mapping of the telepathy provider.

#include "dtmf.h"

#include <QtTest>

class tst_Dtmf : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void keys_data();
    void keys();
};

void tst_Dtmf::keys_data()
{
    QTest::addColumn<QChar>("key");
    QTest::addColumn<int>("event");

    for (int i = 0; i <= 9; ++i)
        QTest::newRow(qPrintable(QString::number(i))) << QChar('0' + i) << i;
    QTest::newRow("star") << QChar('*') << 10;
    QTest::newRow("hash") << QChar('#') << 11;
    QTest::newRow("A") << QChar('A') << 12;
    QTest::newRow("D") << QChar('D') << 15;
    QTest::newRow("a") << QChar('a') << 12;
    QTest::newRow("d") << QChar('d') << 15;
    QTest::newRow("E") << QChar('E') << -1;
    QTest::newRow("minus") << QChar('-') << -1;
    QTest::newRow("space") << QChar(' ') << -1;
    QTest::newRow("arabic-indic 1") << QChar(0x0661) << -1;
}

void tst_Dtmf::keys()
{
    QFETCH(QChar, key);
    QFETCH(int, event);
    QCOMPARE(Dtmf::event(key), event);
}

QTEST_GUILESS_MAIN(tst_Dtmf)

#include "tst_dtmf.moc"
