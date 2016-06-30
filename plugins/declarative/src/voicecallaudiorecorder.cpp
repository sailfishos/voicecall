
#include "voicecallaudiorecorder.h"

#include <QDateTime>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusPendingReply>
#include <QDir>
#include <QLocale>
#include <QDataStream>
#include <QtDebug>

namespace {

const QString recordingsDir("CallRecordings");

const quint16 ChannelCount = 1;
const quint16 SampleRate = 8000;
const quint16 SampleBits = 16;
const quint32 WaveHeaderLength = 44;
const quint16 WavePCMFormat = 1;

const QString RouteManagerService("org.nemomobile.Route.Manager");
const QString RouteManagerPath("/org/nemomobile/Route/Manager");
const QString RouteManagerInterface("org.nemomobile.Route.Manager");

QAudioFormat getRecordingFormat()
{
    QAudioFormat format;

    format.setChannelCount(ChannelCount);
    format.setSampleRate(SampleRate);
    format.setSampleSize(SampleBits);
    format.setCodec(QStringLiteral("audio/pcm"));
    format.setByteOrder(QAudioFormat::LittleEndian);
    format.setSampleType(QAudioFormat::UnSignedInt);

    QAudioDeviceInfo info(QAudioDeviceInfo::defaultInputDevice());
    if (!info.isFormatSupported(format)) {
        format = info.nearestFormat(format);
    }

    return format;
}

const QAudioFormat recordingFormat(getRecordingFormat());

QDBusMessage createEnableVoicecallRecordingMessage(bool enable)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(RouteManagerService,
                                                      RouteManagerPath,
                                                      RouteManagerInterface,
                                                      enable ? QString("Enable") : QString("Disable"));
    msg.setArguments(QVariantList() << QVariant(QString("voicecallrecord")));
    return msg;
}

QDBusMessage createVoicecallFeaturesMessage(void)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(RouteManagerService,
                                                      RouteManagerPath,
                                                      RouteManagerInterface,
                                                      QString("GetAll"));
    return msg;
}

}


struct ManagerFeature
{
    QString name;
    unsigned unused1;
    unsigned unused2;
};
typedef QList<ManagerFeature> ManagerFeatureList;

Q_DECLARE_METATYPE(ManagerFeature)
Q_DECLARE_METATYPE(ManagerFeatureList)

QDBusArgument &operator<<(QDBusArgument &arg, const ManagerFeature &feature)
{
    arg.beginStructure();
    arg << feature.name;
    arg << feature.unused1;
    arg << feature.unused2;
    arg.endStructure();
    return arg;
}

const QDBusArgument &operator>>(const QDBusArgument &arg, ManagerFeature &feature)
{
    arg.beginStructure();
    arg >> feature.name;
    arg >> feature.unused1;
    arg >> feature.unused2;
    arg.endStructure();
    return arg;
}


VoiceCallAudioRecorder::VoiceCallAudioRecorder(QObject *parent)
    : QObject(parent)
    , featureAvailable(false)
    , active(false)
{
    qDBusRegisterMetaType<ManagerFeature>();
    qDBusRegisterMetaType<ManagerFeatureList>();

    QDBusMessage featuresMsg = createVoicecallFeaturesMessage();
    QDBusPendingCall featuresCall = QDBusConnection::systemBus().asyncCall(featuresMsg);
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(featuresCall, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, &VoiceCallAudioRecorder::featuresCallFinished);
}

VoiceCallAudioRecorder::~VoiceCallAudioRecorder()
{
    terminateRecording();
}

bool VoiceCallAudioRecorder::available() const
{
    return featureAvailable;
}

void VoiceCallAudioRecorder::startRecording(const QString &name, const QString &uid, bool incoming)
{
    if (name.isEmpty() || uid.isEmpty()) {
        qWarning() << "Unable to create unidentified recording";
        return;
    }

    if (active) {
        qWarning() << "Recording already in progress";
        return;
    }

    const QString timestamp(QLocale::c().toString(QDateTime::currentDateTime(), QStringLiteral("yyyyMMdd-HHmmsszzz")));
    const QString fileName(QString("%1.%2.%3.%4").arg(name).arg(uid).arg(timestamp).arg(incoming ? 1 : 0));

    if (initiateRecording(fileName)) {
        label = name;
    }
}

void VoiceCallAudioRecorder::stopRecording()
{
    terminateRecording();
}

bool VoiceCallAudioRecorder::recording() const
{
    return active;
}

QString VoiceCallAudioRecorder::decodeRecordingFileName(const QString &fileName)
{
    return QFile::decodeName(fileName.toLocal8Bit());
}

bool VoiceCallAudioRecorder::deleteRecording(const QString &fileName)
{
    const QString outputPath(QDir::home().path() + QDir::separator() + recordingsDir);
    QDir outputDir(outputPath);
    if (!outputDir.isReadable()) {
        // We can't easily test writability
        qWarning() << "Unreadable directory:" << outputDir;
    }

    if (outputDir.exists(fileName)) {
        if (outputDir.remove(fileName)) {
            return true;
        } else {
            qWarning() << "Unable to delete recording file:" << fileName;
        }
    } else {
        qWarning() << "Unable to delete nonexistent recording file:" << fileName;
    }
    return false;
}

void VoiceCallAudioRecorder::featuresCallFinished(QDBusPendingCallWatcher *watcher)
{
    QDBusPendingReply<QString, unsigned, QString, unsigned, ManagerFeatureList> reply = *watcher;
    if (reply.isError()) {
        qWarning() << "Unable to query voice call recording feature.";
    } else {
        const ManagerFeatureList features = reply.argumentAt<4>();
        foreach (const ManagerFeature &feature, features) {
            if (feature.name == QStringLiteral("voicecallrecord")) {
                featureAvailable = true;
                emit availableChanged();
                break;
            }
        }
    }

    watcher->deleteLater();
}

void VoiceCallAudioRecorder::inputStateChanged(QAudio::State state)
{
    if (state == QAudio::StoppedState) {
        if (input) {
            if (input->error() != QAudio::NoError) {
                qWarning() << "Recording stopped due to error:" << input->error();
            }
        }
        terminateRecording();
    }
}

bool VoiceCallAudioRecorder::initiateRecording(const QString &fileName)
{
    terminateRecording();

    if (!QDir::home().exists(recordingsDir)) {
        // Create the directory for recordings
        QDir::home().mkdir(recordingsDir);
        if (!QDir::home().exists(recordingsDir)) {
            qWarning() << "Unable to create:" << recordingsDir;
            emit recordingError(FileCreation);
            return false;
        }
    }
    const QString outputPath(QDir::home().path() + QDir::separator() + recordingsDir);
    QDir outputDir(outputPath);
    if (!outputDir.isReadable()) {
        // We can't easily test writability
        qWarning() << "Unreadable directory:" << outputDir;
    }

    const QString filePath(outputDir.filePath(QString("%1.wav").arg(QString::fromLocal8Bit(QFile::encodeName(fileName)))));

    QScopedPointer<QFile> file(new QFile(filePath));
    if (!file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "Unable to open file for write:" << filePath;
        emit recordingError(FileCreation);
        return false;
    }

    // Leave space for the header to be placed later
    const QByteArray emptyBytes(WaveHeaderLength, '\0');
    if (file->write(emptyBytes) == -1) {
        qWarning() << "Unable to write header space to file:" << filePath;
        emit recordingError(FileCreation);
        return false;
    }

    QDBusMessage enableRecording(createEnableVoicecallRecordingMessage(true));
    if (!QDBusConnection::systemBus().send(enableRecording)) {
        qWarning() << "Unable to request recording activation" << QDBusConnection::systemBus().lastError();
        file->remove();
        emit recordingError(AudioRouting);
        return false;
    }

    output.swap(file);

    input.reset(new QAudioInput(recordingFormat));
    connect(input.data(), &QAudioInput::stateChanged, this, &VoiceCallAudioRecorder::inputStateChanged);

    input->start(output.data());
    active = true;
    emit recordingChanged();

    return true;
}

void VoiceCallAudioRecorder::terminateRecording()
{
    if (input) {
        input->stop();
        input.reset();

        QDBusMessage disableRecording(createEnableVoicecallRecordingMessage(false));
        if (!QDBusConnection::systemBus().send(disableRecording)) {
            qWarning() << "Unable to request recording deactivation" << QDBusConnection::systemBus().lastError();
        }
    }
    if (output) {
        bool success = false;

        // We need to write the header to the file
        const quint32 fileLength = output->size();
        const quint32 dataLength = fileLength - WaveHeaderLength;

        if (dataLength > 0) {
            QByteArray waveHeader;
            {
                QDataStream os(&waveHeader, QIODevice::WriteOnly);
                os.setByteOrder(QDataStream::LittleEndian);

                os.writeRawData("RIFF", 4);
                os << quint32(fileLength - 8);  // Total data length
                os.writeRawData("WAVE", 4);
                os.writeRawData("fmt ", 4);
                os << quint32(16);              // fmt header length
                os << quint16(WavePCMFormat);
                os << quint16(ChannelCount);
                os << quint32(SampleRate);
                os << quint32(SampleRate * ChannelCount * (SampleBits / CHAR_BIT)); // data rate
                os << quint16(SampleBits/ CHAR_BIT); // bytes per sample
                os << quint16(SampleBits);
                os.writeRawData("data", 4);
                os << quint32(dataLength);
            }

            if (output->seek(0) && output->write(waveHeader) == waveHeader.length()) {
                success = true;
            } else {
                qWarning() << "Unable to write header to file:" << output->fileName();
            }
        }

        const QString fileName(output->fileName());
        output->close();
        output.reset();

        if (success) {
            emit callRecorded(fileName, label);
        } else {
            emit recordingError(FileStorage);
        }
    }
    if (active) {
        active = false;
        emit recordingChanged();
    }
}

