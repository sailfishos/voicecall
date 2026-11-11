/*
 * This file is a part of the Voice Call Manager project
 *
 * Copyright (C) 2026 Herman van Hazendonk <github.com@herrie.org>
 *
 * You may use this file under the terms of the BSD license as follows:
 *
 * "Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * The names of its contributors may NOT be used to endorse or promote
 *     products derived from this software without specific prior written
 *     permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE."
 */
#ifndef RECORDINGFILE_H
#define RECORDINGFILE_H

/*
 * File name and WAV header handling for call recordings, kept free of audio
 * and D-Bus so that it can be tested on its own.
 */

#include <QByteArray>
#include <QDataStream>
#include <QIODevice>
#include <QString>
#include <QtGlobal>

namespace RecordingFile {

const int HeaderLength = 44;

// Longest name component in UTF-8 bytes. Two of them, the timestamp,
// direction and ".wav.tmp" stay below NAME_MAX (255 bytes).
const int MaxComponentBytes = 96;

// The remote party's name and id end up in a file name. They come from the
// network or the address book and may hold anything, '/' and ".." included.
inline QString component(const QString &value)
{
    QString result;
    int bytes = 0;
    for (int i = 0; i < value.length(); ++i) {
        const QChar c = value.at(i);
        if (c.isHighSurrogate() && i + 1 < value.length() && value.at(i + 1).isLowSurrogate()) {
            // A character outside the BMP, 4 bytes in UTF-8, kept whole
            if (bytes + 4 > MaxComponentBytes)
                break;
            result.append(c).append(value.at(++i));
            bytes += 4;
            continue;
        }
        const ushort u = c.unicode();
        const int size = u < 0x80 ? 1 : u < 0x800 ? 2 : 3;
        if (bytes + size > MaxComponentBytes)
            break;
        if (c == QLatin1Char('/') || c == QLatin1Char('\\') || u < 0x20 || u == 0x7f
                || c.isSurrogate()) {
            // Path separators, control characters and unpaired surrogates
            result.append(QLatin1Char('_'));
        } else {
            result.append(c);
        }
        bytes += size;
    }
    // No hidden files, and nothing that is "." or ".."
    for (int i = 0; i < result.length() && result.at(i) == QLatin1Char('.'); ++i)
        result[i] = QLatin1Char('_');
    return result;
}

// A recording's file name as passed in from QML: a plain name in the
// recordings directory, never a path.
inline bool isPlainFileName(const QString &fileName)
{
    return !fileName.isEmpty()
            && fileName != QLatin1String(".")
            && fileName != QLatin1String("..")
            && !fileName.contains(QLatin1Char('/'))
            && !fileName.contains(QChar(0));
}

// RIFF/WAVE header for PCM data of dataLength bytes. Returns an empty array
// when the data does not fit a WAV file.
inline QByteArray waveHeader(int channels, int sampleRate, int sampleBits, qint64 dataLength)
{
    const qint64 maxData = qint64(0xffffffffu) - (HeaderLength - 8);
    if (channels <= 0 || sampleRate <= 0 || sampleBits <= 0 || sampleBits % 8
            || dataLength < 0 || dataLength > maxData)
        return QByteArray();

    const quint32 blockAlign = quint32(channels) * quint32(sampleBits / 8);
    QByteArray header;
    QDataStream os(&header, QIODevice::WriteOnly);
    os.setByteOrder(QDataStream::LittleEndian);

    os.writeRawData("RIFF", 4);
    os << quint32(dataLength + HeaderLength - 8);
    os.writeRawData("WAVE", 4);
    os.writeRawData("fmt ", 4);
    os << quint32(16);              // fmt chunk length
    os << quint16(1);               // PCM
    os << quint16(channels);
    os << quint32(sampleRate);
    os << quint32(sampleRate) * blockAlign; // byte rate
    os << quint16(blockAlign);
    os << quint16(sampleBits);
    os.writeRawData("data", 4);
    os << quint32(dataLength);
    return header;
}

}

#endif // RECORDINGFILE_H
