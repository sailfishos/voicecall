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
#ifndef FAKE_QOFONO_H
#define FAKE_QOFONO_H

/*
 * Stand-ins for the parts of libqofono the ofono provider uses. Each object
 * registers itself under its D-Bus path so a test can find the instance the
 * provider created and drive it; "requests" records what was asked of it.
 */

#include <QHash>
#include <QObject>
#include <QStringList>

class QOfonoVoiceCall : public QObject
{
    Q_OBJECT

public:
    explicit QOfonoVoiceCall(QObject *parent = 0) : QObject(parent) {}
    ~QOfonoVoiceCall() { instances().remove(m_path); }

    // Calls whose properties are known before the object is created
    static QHash<QString, QString> &presetStates()
    {
        static QHash<QString, QString> states;
        return states;
    }
    static QHash<QString, QOfonoVoiceCall *> &instances()
    {
        static QHash<QString, QOfonoVoiceCall *> calls;
        return calls;
    }

    void setVoiceCallPath(const QString &path)
    {
        m_path = path;
        instances().insert(path, this);
        if (presetStates().contains(path)) {
            m_state = presetStates().value(path);
            m_valid = true;
        }
    }
    QString voiceCallPath() const { return m_path; }
    bool isValid() const { return m_valid; }
    QString state() const { return m_state; }
    QString lineIdentification() const { return QStringLiteral("+358401234567"); }
    QString startTime() const { return m_startTime; }
    bool multiparty() const { return multipartyCall; }
    bool emergency() const { return false; }

    void answer() { requests << QStringLiteral("answer"); }
    void hangup() { requests << QStringLiteral("hangup"); }
    void deflect(const QString &) { requests << QStringLiteral("deflect"); }

    // Test controls
    void becomeValid(const QString &state)
    {
        m_state = state;
        m_valid = true;
        emit validChanged(true);
    }
    void becomeInvalid()
    {
        m_valid = false;
        emit validChanged(false);
    }
    void changeState(const QString &state)
    {
        m_state = state;
        emit stateChanged(state);
    }
    void changeStartTime(const QString &time)
    {
        m_startTime = time;
        emit startTimeChanged(time);
    }

    QStringList requests;
    bool multipartyCall = false;

Q_SIGNALS:
    void lineIdentificationChanged(const QString &);
    void emergencyChanged(bool);
    void multipartyChanged(bool);
    void stateChanged(const QString &);
    void validChanged(bool);
    void startTimeChanged(const QString &);

private:
    QString m_path;
    QString m_state;
    QString m_startTime;
    bool m_valid = false;
};

class QOfonoVoiceCallManager : public QObject
{
    Q_OBJECT

public:
    explicit QOfonoVoiceCallManager(QObject *parent = 0) : QObject(parent) { last() = this; }
    ~QOfonoVoiceCallManager() { if (last() == this) last() = 0; }

    static QOfonoVoiceCallManager *&last()
    {
        static QOfonoVoiceCallManager *manager = 0;
        return manager;
    }

    void setModemPath(const QString &path) { m_path = path; }
    // Reads a member, so a call through a dangling or null pointer fails
    bool isValid() const { return valid; }
    QStringList getCalls() const { return calls; }
    QString errorMessage() const { return error; }

    void dial(const QString &number, const QString &) { requests << QStringLiteral("dial:") + number; }
    void holdAndAnswer() { requests << QStringLiteral("holdAndAnswer"); }
    void swapCalls() { requests << QStringLiteral("swapCalls"); }
    void sendTones(const QString &tones) { requests << QStringLiteral("tones:") + tones; }
    void createMultiparty() { requests << QStringLiteral("createMultiparty"); }
    void privateChat(const QString &call) { requests << QStringLiteral("privateChat:") + call; }

    QStringList calls;
    QString error;
    QStringList requests;
    bool valid = true;

Q_SIGNALS:
    void callAdded(const QString &call);
    void callRemoved(const QString &call);
    void dialComplete(bool status);
    void createMultipartyComplete(bool status, const QStringList &calls);
    void privateChatComplete(bool status, const QStringList &calls);

private:
    QString m_path;
};

class QOfonoModem : public QObject
{
    Q_OBJECT

public:
    explicit QOfonoModem(QObject *parent = 0) : QObject(parent) { last() = this; }
    ~QOfonoModem() { if (last() == this) last() = 0; }

    static QOfonoModem *&last()
    {
        static QOfonoModem *modem = 0;
        return modem;
    }

    void setModemPath(const QString &path) { m_path = path; }
    QString modemPath() const { return m_path; }
    QStringList interfaces() const { return m_interfaces; }

    // Interfaces the next modem starts with
    static QStringList &initialInterfaces()
    {
        static QStringList interfaces;
        return interfaces;
    }

    void changeInterfaces(const QStringList &interfaces)
    {
        m_interfaces = interfaces;
        emit interfacesChanged(interfaces);
    }

Q_SIGNALS:
    void interfacesChanged(const QStringList &interfaces);

private:
    QString m_path;
    QStringList m_interfaces = initialInterfaces();
};

class QOfonoManager : public QObject
{
    Q_OBJECT

public:
    explicit QOfonoManager(QObject *parent = 0) : QObject(parent), m_modems(initialModems()) { last() = this; }
    ~QOfonoManager() { if (last() == this) last() = 0; }

    static QOfonoManager *&last()
    {
        static QOfonoManager *manager = 0;
        return manager;
    }
    // Modems the next manager already knows when it is created
    static QStringList &initialModems()
    {
        static QStringList modems;
        return modems;
    }

    QStringList modems() const { return m_modems; }

    // Test controls, emitting what libqofono emits in each case:
    // the GetModems reply (at start and when oFono reappears) only
    // changes the list; ModemAdded and ModemRemoved also name the modem;
    // oFono going away removes each modem.
    void getModemsReply(const QStringList &modems)
    {
        m_modems = modems;
        m_modems.sort();
        emit modemsChanged(m_modems);
    }
    void addModem(const QString &path)
    {
        m_modems.append(path);
        m_modems.sort();
        emit modemAdded(path);
        emit modemsChanged(m_modems);
    }
    void removeModem(const QString &path)
    {
        if (m_modems.removeOne(path)) {
            emit modemRemoved(path);
            emit modemsChanged(m_modems);
        }
    }
    void ofonoGone()
    {
        const QStringList modems = m_modems;
        for (const QString &path : modems)
            emit modemRemoved(path);
        m_modems.clear();
        emit modemsChanged(m_modems);
    }

Q_SIGNALS:
    void modemAdded(const QString &path);
    void modemRemoved(const QString &path);
    void modemsChanged(const QStringList &modems);

private:
    QStringList m_modems;
};

#endif // FAKE_QOFONO_H
