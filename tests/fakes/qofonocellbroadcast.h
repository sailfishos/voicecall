/*
 * Copyright (C) 2026 Jolla Mobile Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef FAKE_QOFONOCELLBROADCAST_H
#define FAKE_QOFONOCELLBROADCAST_H

#include <QDBusError>
#include <QList>
#include <QObject>
#include <QVariantMap>

class QOfonoObject : public QObject
{
public:
    explicit QOfonoObject(QObject *parent = 0)
        : QObject(parent)
    {
    }

protected:
    virtual void dbusInterfaceDropped()
    {
    }

    virtual void setPropertyFinished(const QString &, const QDBusError *)
    {
    }
};

class QOfonoCellBroadcast : public QOfonoObject
{
    Q_OBJECT

public:
    struct PropertyRequest {
        QString property;
        QVariant value;
    };

    explicit QOfonoCellBroadcast(QObject *parent = 0)
        : QOfonoObject(parent)
        , m_enabled(false)
        , m_valid(false)
        , m_interfaceAttached(false)
        , m_pendingQueryCount(0)
    {
        instanceStorage() = this;
    }

    ~QOfonoCellBroadcast()
    {
        if (instanceStorage() == this) {
            instanceStorage() = 0;
        }
    }

    static QOfonoCellBroadcast *testInstance()
    {
        return instanceStorage();
    }

    void setModemPath(const QString &path)
    {
        if (m_modemPath == path) {
            return;
        }
        m_modemPath = path;
        m_interfaceAttached = !path.isEmpty();
        if (m_interfaceAttached) {
            // QOfono implicitly queries properties when the modem interface
            // is attached.
            ++m_pendingQueryCount;
        }
    }

    void queryProperties()
    {
        if (m_interfaceAttached) {
            ++m_pendingQueryCount;
        }
    }

    int pendingQueryCount() const
    {
        return m_pendingQueryCount;
    }

    bool enabled() const
    {
        return m_enabled;
    }

    QString topics() const
    {
        return m_topics;
    }

    void setEnabled(bool enabled)
    {
        if (!m_interfaceAttached) {
            return;
        }
        PropertyRequest request;
        request.property = QStringLiteral("Powered");
        request.value = enabled;
        m_requests.append(request);
    }

    void setTopics(const QString &topics)
    {
        if (!m_interfaceAttached) {
            return;
        }
        PropertyRequest request;
        request.property = QStringLiteral("Topics");
        request.value = topics;
        m_requests.append(request);
    }

    int requestCount() const
    {
        return m_requests.count();
    }

    PropertyRequest firstRequest() const
    {
        return m_requests.isEmpty() ? PropertyRequest() : m_requests.first();
    }

    void applyFirstRequest()
    {
        if (m_requests.isEmpty()) {
            return;
        }
        const PropertyRequest request = m_requests.first();
        if (request.property == QLatin1String("Powered")) {
            setEnabledProperty(request.value.toBool());
        } else if (request.property == QLatin1String("Topics")) {
            setTopicsProperty(request.value.toString());
        }
    }

    void finishFirstRequest(const QString &errorString = QString())
    {
        if (m_requests.isEmpty()) {
            return;
        }
        const PropertyRequest request = m_requests.takeFirst();
        if (errorString.isEmpty()) {
            setPropertyFinished(request.property, 0);
        } else {
            const QDBusError error(QDBusError::Failed, errorString);
            setPropertyFinished(request.property, &error);
        }
    }

    void finishGetProperties(
            bool enabled, const QString &topics,
            const QString &errorString = QString(),
            QDBusError::ErrorType errorType = QDBusError::Failed)
    {
        Q_ASSERT(m_pendingQueryCount > 0);
        --m_pendingQueryCount;
        QVariantMap properties;
        properties.insert(QStringLiteral("Powered"), enabled);
        properties.insert(QStringLiteral("Topics"), topics);
        if (errorString.isEmpty()) {
            getPropertiesFinished(properties, 0);
        } else {
            const QDBusError error(errorType, errorString);
            getPropertiesFinished(properties, &error);
        }
    }

    void dropInterface()
    {
        if (!m_interfaceAttached) {
            return;
        }
        m_interfaceAttached = false;
        m_pendingQueryCount = 0;
        m_requests.clear();
        dbusInterfaceDropped();
        if (m_valid) {
            m_valid = false;
            Q_EMIT validChanged(false);
        }
    }

    void reattachInterface()
    {
        if (m_interfaceAttached || m_modemPath.isEmpty()) {
            return;
        }
        m_interfaceAttached = true;
        // Match QOfono's implicit GetProperties request on interface attach.
        ++m_pendingQueryCount;
    }

    void changeEnabled(bool enabled)
    {
        setEnabledProperty(enabled);
    }

    void changeTopics(const QString &topics)
    {
        setTopicsProperty(topics);
    }

Q_SIGNALS:
    void topicsChanged(const QString &topics);
    void enabledChanged(bool enabled);
    void validChanged(bool valid);
    void incomingBroadcast(const QString &text, quint16 channel);
    void incomingBroadcastWithProperties(const QString &text,
                                         const QVariantMap &properties);
    void emergencyBroadcast(const QString &text,
                            const QVariantMap &properties);

protected:
    void dbusInterfaceDropped() override
    {
        QOfonoObject::dbusInterfaceDropped();
        setEnabledProperty(false);
        setTopicsProperty(QString());
    }

    virtual void getPropertiesFinished(const QVariantMap &properties,
                                       const QDBusError *error)
    {
        if (error) {
            switch (error->type()) {
            case QDBusError::NoReply:
            case QDBusError::Timeout:
            case QDBusError::TimedOut:
                // Match libqofono, which immediately replaces a timed-out
                // GetProperties request.
                ++m_pendingQueryCount;
                break;
            default:
                break;
            }
            return;
        }
        setEnabledProperty(properties.value(QStringLiteral("Powered")).toBool());
        setTopicsProperty(properties.value(QStringLiteral("Topics")).toString());
        if (!m_valid) {
            m_valid = true;
            Q_EMIT validChanged(true);
        }
    }

private:
    static QOfonoCellBroadcast *&instanceStorage()
    {
        static QOfonoCellBroadcast *instance = 0;
        return instance;
    }

    void setEnabledProperty(bool enabled)
    {
        if (m_enabled == enabled) {
            return;
        }
        m_enabled = enabled;
        Q_EMIT enabledChanged(enabled);
    }

    void setTopicsProperty(const QString &topics)
    {
        if (m_topics == topics) {
            return;
        }
        m_topics = topics;
        Q_EMIT topicsChanged(topics);
    }

private:
    QString m_modemPath;
    QString m_topics;
    bool m_enabled;
    bool m_valid;
    bool m_interfaceAttached;
    int m_pendingQueryCount;
    QList<PropertyRequest> m_requests;
};

#endif
