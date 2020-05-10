/*
 * This file is a part of the Voice Call Manager project
 *
 * Copyright (c) 2016 - 2019 Jolla Ltd.
 * Copyright (c) 2020 Open Mobile Platform LLC.
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

#ifndef VOICECALLAUDIORECORDER_H
#define VOICECALLAUDIORECORDER_H

#include <QAudioInput>
#include <QFile>
#include <QScopedPointer>
#include <QDBusPendingCallWatcher>

class VoiceCallAudioRecorder : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY(VoiceCallAudioRecorder)

    Q_ENUMS(ErrorCondition)

    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    Q_PROPERTY(bool recording READ recording NOTIFY recordingChanged)
    Q_PROPERTY(QString recordingsDirPath READ recordingsDirPath CONSTANT)

public:
    enum ErrorCondition {
        FileCreation,
        FileStorage,
        AudioRouting,
    };

    explicit VoiceCallAudioRecorder(QObject *parent);
    ~VoiceCallAudioRecorder();

    bool available() const;

    Q_INVOKABLE void startRecording(const QString &name, const QString &uid, bool incoming);
    Q_INVOKABLE void stopRecording();

    bool recording() const;
    QString recordingsDirPath() const;

    Q_INVOKABLE QString decodeRecordingFileName(const QString &fileName);
    Q_INVOKABLE bool deleteRecording(const QString &fileName);

signals:
    void availableChanged();
    void recordingChanged();
    void recordingError(ErrorCondition error);
    void callRecorded(const QString &fileName, const QString &label);

private slots:
    void featuresCallFinished(QDBusPendingCallWatcher *watcher);
    void inputStateChanged(QAudio::State state);

private:
    bool initiateRecording(const QString &fileName);
    void terminateRecording();

    QScopedPointer<QAudioInput> input;
    QScopedPointer<QFile> output;
    QString label;
    bool featureAvailable;
    bool active;
};

#endif
