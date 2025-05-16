#ifndef VOICECALLHANDLER_H
#define VOICECALLHANDLER_H

#include <QObject>
#include <QDateTime>

#include <QDBusInterface>
#include <QDBusPendingCallWatcher>

class VoiceCallModel;

class VoiceCallHandler : public QObject
{
    Q_OBJECT

    Q_ENUMS(VoiceCallStatus)

    Q_PROPERTY(QString handlerId READ handlerId CONSTANT)
    Q_PROPERTY(QString providerId READ providerId CONSTANT)
    Q_PROPERTY(int status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
    Q_PROPERTY(QString lineId READ lineId NOTIFY lineIdChanged)
    Q_PROPERTY(QDateTime startedAt READ startedAt NOTIFY startedAtChanged)
    Q_PROPERTY(int duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(bool isIncoming READ isIncoming CONSTANT)
    Q_PROPERTY(bool isEmergency READ isEmergency NOTIFY emergencyChanged)
    Q_PROPERTY(bool isMultiparty READ isMultiparty NOTIFY multipartyChanged)
    Q_PROPERTY(bool isForwarded READ isForwarded NOTIFY forwardedChanged)
    Q_PROPERTY(bool isRemoteHeld READ isRemoteHeld NOTIFY remoteHeldChanged)
    Q_PROPERTY(VoiceCallModel* childCalls READ childCalls NOTIFY childCallsChanged)
    Q_PROPERTY(VoiceCallHandler* parentCall READ parentCall NOTIFY parentCallChanged)

public:
    enum VoiceCallStatus {
        STATUS_NULL,
        STATUS_ACTIVE,
        STATUS_HELD,
        STATUS_DIALING,
        STATUS_ALERTING,
        STATUS_INCOMING,
        STATUS_WAITING,
        STATUS_DISCONNECTED
    };

    explicit VoiceCallHandler(const QString &handlerId, QObject *parent = 0);
    ~VoiceCallHandler();

    QDBusInterface* interface() const;

    QString handlerId() const;
    QString providerId() const;
    int status() const;
    QString statusText() const;
    QString lineId() const;
    QDateTime startedAt() const;
    int duration() const;
    bool isIncoming() const;
    bool isMultiparty() const;
    bool isEmergency() const;
    bool isForwarded() const;
    bool isRemoteHeld() const;
    VoiceCallModel* childCalls() const;
    VoiceCallHandler* parentCall() const;

Q_SIGNALS:
    void error(const QString &error);
    void statusChanged();
    void lineIdChanged();
    void durationChanged();
    void startedAtChanged();
    void emergencyChanged();
    void multipartyChanged();
    void forwardedChanged();
    void remoteHeldChanged();
    void childCallsChanged();
    void childCallsListChanged();
    void parentCallChanged();

public Q_SLOTS:
    void answer();
    void hangup();
    void hold(bool on);
    void deflect(const QString &target);
    void sendDtmf(const QString &tones);
    void merge(const QString &callHandle);
    void split();

private Q_SLOTS:
    void initialize();

    void onPendingCallFinished(QDBusPendingCallWatcher *watcher);
    void onPendingVoidCallFinished(QDBusPendingCallWatcher *watcher);
    void onDurationChanged(int duration);
    void onStatusChanged(int status, const QString &statusText);
    void onLineIdChanged(const QString &lineId);
    void onStartedAtChanged(const QDateTime &startedAt);
    void onEmergencyChanged(bool emergency);
    void onMultipartyChanged(bool multiparty);
    void onForwardedChanged(bool forwarded);
    void onRemoteHeldChanged(bool remoteHeld);
    void onMultipartyHandlerIdChanged(QString handlerId);
    void onChildCallsChanged(const QStringList &);
    void onGetPropertiesFinished(QDBusPendingCallWatcher *watcher);

private:
    class VoiceCallHandlerPrivate *d_ptr;

    Q_DISABLE_COPY(VoiceCallHandler)
    Q_DECLARE_PRIVATE(VoiceCallHandler)
};

#endif // VOICECALLHANDLER_H
