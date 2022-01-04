/*
 * This file is a part of the Voice Call Manager project
 *
 * Copyright (c) 2016 - 2019 Jolla Ltd.
 * Copyright (c) 2020 Open Mobile Platform LLC.
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
