/*
 * Copyright (C) 2026 Jolla Mobile Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef FAKE_MDCONFITEM_H
#define FAKE_MDCONFITEM_H

#include <QHash>
#include <QObject>
#include <QVariant>

class MDConfItem : public QObject
{
    Q_OBJECT

public:
    explicit MDConfItem(const QString &key, QObject *parent = 0)
        : QObject(parent)
        , m_key(key)
    {
    }

    QVariant value(const QVariant &defaultValue = QVariant()) const
    {
        return values().value(m_key, defaultValue);
    }

    void set(const QVariant &value)
    {
        if (values().value(m_key) == value && values().contains(m_key)) {
            return;
        }
        values().insert(m_key, value);
        Q_EMIT valueChanged();
    }

    static void clear()
    {
        values().clear();
    }

Q_SIGNALS:
    void valueChanged();

private:
    static QHash<QString, QVariant> &values()
    {
        static QHash<QString, QVariant> storage;
        return storage;
    }

private:
    QString m_key;
};

#endif
