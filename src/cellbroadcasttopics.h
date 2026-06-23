/*
 * This file is a part of the Voice Call Manager project
 *
 * Copyright (C) 2026 Jolla Mobile Ltd
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
#ifndef CELLBROADCASTTOPICS_H
#define CELLBROADCASTTOPICS_H

#include <QList>
#include <QString>
#include <QVariantList>

struct CellBroadcastTopicRange
{
    int from;
    int to;

    CellBroadcastTopicRange();
    CellBroadcastTopicRange(int first, int last);
};

typedef QList<CellBroadcastTopicRange> CellBroadcastTopicRangeList;

namespace CellBroadcastTopics
{
    CellBroadcastTopicRangeList parse(const QString &topics);
    QString format(const CellBroadcastTopicRangeList &ranges);
    QString rangeToString(const CellBroadcastTopicRange &range);
    CellBroadcastTopicRangeList normalize(const CellBroadcastTopicRangeList &ranges);
    CellBroadcastTopicRangeList unite(const CellBroadcastTopicRangeList &left,
                                      const CellBroadcastTopicRangeList &right);
    CellBroadcastTopicRangeList subtract(const CellBroadcastTopicRangeList &ranges,
                                         const CellBroadcastTopicRangeList &remove);
    bool equals(const CellBroadcastTopicRangeList &left,
                const CellBroadcastTopicRangeList &right);
    QVariantList toVariantList(const CellBroadcastTopicRangeList &ranges);
}

#endif
