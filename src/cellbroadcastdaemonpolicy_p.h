/*
 * Copyright (C) 2026 Jolla Mobile Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef CELLBROADCASTDAEMONPOLICY_P_H
#define CELLBROADCASTDAEMONPOLICY_P_H

#include <QVariantMap>

inline bool cellBroadcastNeedsEmergencyAttention(const QVariantMap &properties,
                                                  bool attentionAdded)
{
    return !attentionAdded
            && (properties.value(QStringLiteral("EmergencyAlert")).toBool()
                || properties.value(QStringLiteral("Primary")).toBool());
}

#endif
