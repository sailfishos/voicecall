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
#include "recordingfile.h"

#include <QtEndian>
#include <QtTest>

class tst_RecordingFile : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void component_data();
    void component();
    void componentLength();
    void plainFileName_data();
    void plainFileName();
    void header();
    void headerLargeData();
    void headerRejectsBadInput_data();
    void headerRejectsBadInput();
};

void tst_RecordingFile::component_data()
{
    QTest::addColumn<QString>("value");
    QTest::addColumn<QString>("expected");

    QTest::newRow("plain") << "Alice" << "Alice";
    QTest::newRow("number") << "+31201234567" << "+31201234567";
    QTest::newRow("slash") << "AC/DC" << "AC_DC";
    QTest::newRow("traversal") << "../../.config/x" << "___.._.config_x";
    QTest::newRow("dotdot") << ".." << "__";
    QTest::newRow("hidden") << ".profile" << "_profile";
    QTest::newRow("backslash") << "a\\b" << "a_b";
    QTest::newRow("newline") << "a\nb" << "a_b";
    QTest::newRow("nul") << QString::fromUtf8("a\0b", 3) << "a_b";
    QTest::newRow("unicode") << QString::fromUtf8("Zoë 李") << QString::fromUtf8("Zoë 李");
}

void tst_RecordingFile::component()
{
    QFETCH(QString, value);
    QFETCH(QString, expected);
    const QString result = RecordingFile::component(value);
    QCOMPARE(result, expected);
    QVERIFY(RecordingFile::isPlainFileName(result) || result.isEmpty());
}

void tst_RecordingFile::componentLength()
{
    const QString cjk(1000, QChar(0x674e));         // 3 bytes each in UTF-8
    const QString ascii(1000, QLatin1Char('a'));
    QString emoji;                                   // 4 bytes, a surrogate pair
    for (int i = 0; i < 300; ++i)
        emoji += QString::fromUtf8("\xf0\x9f\x93\x9e");

    for (const QString &name : { cjk, ascii, emoji }) {
        const QString result = RecordingFile::component(name);
        QVERIFY(result.toUtf8().size() <= RecordingFile::MaxComponentBytes);
        QVERIFY(result.toUtf8().size() > RecordingFile::MaxComponentBytes - 4);
        // Nothing cut in half
        QCOMPARE(QString::fromUtf8(result.toUtf8()), result);
        QVERIFY(!result.contains(QLatin1Char('_')));

        // Two components, the timestamp, direction and suffix fit NAME_MAX
        const QString fileName = QStringLiteral("%1.%2.20260101-120000000.1.wav.tmp")
                .arg(result, result);
        QVERIFY(fileName.toUtf8().size() <= 255);
    }

    // An unpaired surrogate is replaced, not passed on
    QCOMPARE(RecordingFile::component(QString(QChar(0xd83d))), QStringLiteral("_"));
}

void tst_RecordingFile::plainFileName_data()
{
    QTest::addColumn<QString>("fileName");
    QTest::addColumn<bool>("plain");

    QTest::newRow("recording") << "Alice.123.20260101-120000000.1.wav" << true;
    QTest::newRow("empty") << "" << false;
    QTest::newRow("dot") << "." << false;
    QTest::newRow("dotdot") << ".." << false;
    QTest::newRow("parent") << "../x.wav" << false;
    QTest::newRow("absolute") << "/etc/passwd" << false;
    QTest::newRow("subdir") << "a/b.wav" << false;
    QTest::newRow("nul") << QString::fromUtf8("a\0b", 3) << false;
}

void tst_RecordingFile::plainFileName()
{
    QFETCH(QString, fileName);
    QFETCH(bool, plain);
    QCOMPARE(RecordingFile::isPlainFileName(fileName), plain);
}

static quint32 u32(const QByteArray &h, int offset)
{
    return qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(h.constData() + offset));
}

static quint16 u16(const QByteArray &h, int offset)
{
    return qFromLittleEndian<quint16>(reinterpret_cast<const uchar *>(h.constData() + offset));
}

void tst_RecordingFile::header()
{
    const QByteArray h = RecordingFile::waveHeader(1, 8000, 16, 16000);
    QCOMPARE(h.size(), RecordingFile::HeaderLength);
    QCOMPARE(h.mid(0, 4), QByteArray("RIFF"));
    QCOMPARE(u32(h, 4), quint32(16000 + 36));
    QCOMPARE(h.mid(8, 8), QByteArray("WAVEfmt "));
    QCOMPARE(u32(h, 16), quint32(16));
    QCOMPARE(u16(h, 20), quint16(1));
    QCOMPARE(u16(h, 22), quint16(1));
    QCOMPARE(u32(h, 24), quint32(8000));
    QCOMPARE(u32(h, 28), quint32(16000));   // byte rate
    QCOMPARE(u16(h, 32), quint16(2));       // block align
    QCOMPARE(u16(h, 34), quint16(16));
    QCOMPARE(h.mid(36, 4), QByteArray("data"));
    QCOMPARE(u32(h, 40), quint32(16000));

    // A format other than the one asked for is described as it is
    const QByteArray s = RecordingFile::waveHeader(2, 16000, 16, 64000);
    QCOMPARE(u16(s, 22), quint16(2));
    QCOMPARE(u32(s, 24), quint32(16000));
    QCOMPARE(u32(s, 28), quint32(64000));
    QCOMPARE(u16(s, 32), quint16(4));
}

void tst_RecordingFile::headerLargeData()
{
    // Above 2 GiB the old int arithmetic went negative
    const qint64 big = Q_INT64_C(3000000000);
    const QByteArray h = RecordingFile::waveHeader(1, 8000, 16, big);
    QCOMPARE(h.size(), RecordingFile::HeaderLength);
    QCOMPARE(u32(h, 40), quint32(big));
    QCOMPARE(u32(h, 4), quint32(big + 36));

    const qint64 max = Q_INT64_C(0xffffffff) - 36;
    QCOMPARE(RecordingFile::waveHeader(1, 8000, 16, max).size(), RecordingFile::HeaderLength);
    QVERIFY(RecordingFile::waveHeader(1, 8000, 16, max + 1).isEmpty());
}

void tst_RecordingFile::headerRejectsBadInput_data()
{
    QTest::addColumn<int>("channels");
    QTest::addColumn<int>("rate");
    QTest::addColumn<int>("bits");
    QTest::addColumn<qint64>("length");

    QTest::newRow("no channels") << 0 << 8000 << 16 << qint64(2);
    QTest::newRow("no rate") << 1 << 0 << 16 << qint64(2);
    QTest::newRow("odd bits") << 1 << 8000 << 12 << qint64(2);
    QTest::newRow("negative") << 1 << 8000 << 16 << qint64(-1);
}

void tst_RecordingFile::headerRejectsBadInput()
{
    QFETCH(int, channels);
    QFETCH(int, rate);
    QFETCH(int, bits);
    QFETCH(qint64, length);
    QVERIFY(RecordingFile::waveHeader(channels, rate, bits, length).isEmpty());
}

QTEST_GUILESS_MAIN(tst_RecordingFile)

#include "tst_recordingfile.moc"
