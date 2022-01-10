/****************************************************************************
**
** Copyright (C) 2011-2012 Tom Swindell <t.swindell@rubyx.co.uk>
** All rights reserved.
**
** This file is part of the Voice Call UI project.
**
** You may use this file under the terms of the BSD license as follows:
**
** "Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions are
** met:
**   * Redistributions of source code must retain the above copyright
**     notice, this list of conditions and the following disclaimer.
**   * Redistributions in binary form must reproduce the above copyright
**     notice, this list of conditions and the following disclaimer in
**     the documentation and/or other materials provided with the
**     distribution.
**   * The names of its contributors may NOT be used to endorse or promote
**     products derived from this software without specific prior written
**     permission.
**
** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
** "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
** LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
** A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
** OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
** SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
** LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
** OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE."
**
****************************************************************************/

#include <QStringList>

#include "common.h"
#include "voicecallmodel.h"

#include "voicecallmanager.h"

#include <QSharedPointer>

class VoiceCallModelPrivate
{
    Q_DECLARE_PUBLIC(VoiceCallModel)

public:
    VoiceCallModelPrivate(VoiceCallModel *q, VoiceCallManager *pManager)
        : q_ptr(q), manager(pManager), confHandler(0)
    {/*...*/}

    VoiceCallModelPrivate(VoiceCallModel *q, VoiceCallHandler *pConf)
        : q_ptr(q), manager(0), confHandler(pConf)
    {/*...*/}

    VoiceCallModel *q_ptr;

    VoiceCallManager *manager;
    VoiceCallHandler *confHandler;

    QList<QSharedPointer<VoiceCallHandler>> handlers;

    QHash<int, QByteArray> headerData;
};

VoiceCallModel::VoiceCallModel(VoiceCallManager *manager)
    : QAbstractListModel(manager), d_ptr(new VoiceCallModelPrivate(this, manager))
{
    TRACE
    Q_D(VoiceCallModel);
    init();
    // Need to listen for signal on the manager, because it handles connectivity to VCM.
    QObject::connect(d->manager, SIGNAL(voiceCallsChanged()), SLOT(onVoiceCallsChanged()));
}

VoiceCallModel::VoiceCallModel(VoiceCallHandler *conf)
    : QAbstractListModel(conf), d_ptr(new VoiceCallModelPrivate(this, conf))
{
    TRACE
    Q_D(VoiceCallModel);
    init();
    QObject::connect(d->confHandler, SIGNAL(childCallsListChanged()), SLOT(onVoiceCallsChanged()));
}

VoiceCallModel::~VoiceCallModel()
{
    TRACE
    Q_D(VoiceCallModel);
    delete d;
}

void VoiceCallModel::init()
{
    d_ptr->headerData.insert(ROLE_ID, "id");
    d_ptr->headerData.insert(ROLE_PROVIDER_ID, "providerId");
    d_ptr->headerData.insert(ROLE_HANDLER_ID, "handlerId");
    d_ptr->headerData.insert(ROLE_STATUS, "status");
    d_ptr->headerData.insert(ROLE_STATUS_TEXT, "statusText");
    d_ptr->headerData.insert(ROLE_LINE_ID, "lineId");
    d_ptr->headerData.insert(ROLE_STARTED_AT, "startedAt");
    d_ptr->headerData.insert(ROLE_IS_EMERGENCY, "isEmergency");
    d_ptr->headerData.insert(ROLE_IS_MULTIPARTY, "isMultiparty");
    d_ptr->headerData.insert(ROLE_INSTANCE, "instance");
    d_ptr->headerData.insert(ROLE_PARENT_CALL, "parentCall");
}

QHash<int, QByteArray> VoiceCallModel::roleNames() const
{
    Q_D(const VoiceCallModel);
    return d->headerData;
}

int VoiceCallModel::count() const
{
    return this->rowCount(QModelIndex());
}

int VoiceCallModel::rowCount(const QModelIndex &parent) const
{
    Q_D(const VoiceCallModel);
    Q_UNUSED(parent)
    return d->handlers.count();
}

QVariant VoiceCallModel::data(const QModelIndex &index, int role) const
{
    Q_D(const VoiceCallModel);
    if (!index.isValid() || index.row() >= d->handlers.count())
        return QVariant();

    VoiceCallHandler *handler = this->instance(index.row());

    switch(role)
    {
    case Qt::DisplayRole:
        return QVariant(handler->lineId());
    case ROLE_PROVIDER_ID:
        return QVariant(handler->providerId());
    case ROLE_HANDLER_ID:
        return QVariant(handler->handlerId());
    case ROLE_STATUS:
        return QVariant(handler->status());
    case ROLE_STATUS_TEXT:
        return QVariant(handler->statusText());
    case ROLE_LINE_ID:
        return QVariant(handler->lineId());
    case ROLE_STARTED_AT:
        return QVariant(handler->startedAt());
    case ROLE_IS_EMERGENCY:
        return QVariant(handler->isEmergency());
    case ROLE_IS_MULTIPARTY:
        return QVariant(handler->isMultiparty());
    case ROLE_PARENT_CALL:
        return QVariant::fromValue(static_cast<QObject*>(handler->parentCall()));
    case ROLE_INSTANCE:
        return QVariant::fromValue(static_cast<QObject*>(handler));
    default:
        return QVariant();
    }
}

void VoiceCallModel::onVoiceCallsChanged()
{
    TRACE
    Q_D(VoiceCallModel);
    QStringList nIds;
    QStringList oIds;

    QStringList added;
    QStringList removed;

    if (d->manager)
        nIds = d->manager->interface()->property("voiceCalls").toStringList();
    else
        nIds = d->confHandler->interface()->property("childCalls").toStringList();

    // Map current call handlers to handler ids for easy indexing.
    foreach (QSharedPointer<VoiceCallHandler> handler, d->handlers) {
        oIds.append(handler->handlerId());
    }

    // Index new handlers to be added.
    foreach (QString nId, nIds) {
        if (!oIds.contains(nId)) added.append(nId);
    }

    // Index old handlers to be removed.
    foreach (QString oId, oIds) {
        if (!nIds.contains(oId)) removed.append(oId);
    }

    // Remove handlers that need to be removed.
    foreach (QString removeId, removed) {
        for (int i = 0; i < d->handlers.count(); ++i) {
            VoiceCallHandler *handler = d->handlers.at(i).data();
            if (handler->handlerId() == removeId) {
                beginRemoveRows(QModelIndex(), i, i);
                handler->disconnect(this);
                d->handlers.removeAt(i);
                endRemoveRows();
                break;
            }
        }
    }

    if (added.count())
        beginInsertRows(QModelIndex(), d->handlers.count(), d->handlers.count() + added.count() - 1);
    // Add handlers that need to be added.
    foreach (QString addId, added) {
        QSharedPointer<VoiceCallHandler> handler = VoiceCallManager::getCallHandler(addId);
        connect(handler.data(), SIGNAL(emergencyChanged()), this, SLOT(propertyChanged()));
        connect(handler.data(), SIGNAL(lineIdChanged()), this, SLOT(propertyChanged()));
        connect(handler.data(), SIGNAL(multipartyChanged()), this, SLOT(propertyChanged()));
        connect(handler.data(), SIGNAL(startedAtChanged()), this, SLOT(propertyChanged()));
        connect(handler.data(), SIGNAL(statusChanged()), this, SLOT(propertyChanged()));
        connect(handler.data(), SIGNAL(parentCallChanged()), this, SLOT(propertyChanged()));
        d->handlers.append(handler);
    }
    if (added.count())
        endInsertRows();

    emit this->countChanged();
}

VoiceCallHandler* VoiceCallModel::instance(int index) const
{
    Q_D(const VoiceCallModel);
    return d->handlers.value(index).data();
}

VoiceCallHandler* VoiceCallModel::instance(const QString &handlerId) const
{
    Q_D(const VoiceCallModel);
    foreach (QSharedPointer<VoiceCallHandler> handler, d->handlers) {
        if (handler->handlerId() == handlerId)
            return handler.data();
    }

    return NULL;
}

void VoiceCallModel::propertyChanged()
{
    TRACE
    Q_D(VoiceCallModel);
    VoiceCallHandler *handler = qobject_cast<VoiceCallHandler*>(sender());
    if (handler) {
        for (int i = 0; i < d->handlers.count(); ++i) {
            if (d->handlers.at(i) == handler) {
                emit dataChanged(index(i, 0), index(i, 0));
                break;
            }
        }
    }
}
