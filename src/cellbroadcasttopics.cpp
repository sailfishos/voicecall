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
#include "cellbroadcasttopics.h"

#include <algorithm>

#include <QStringList>
#include <QVariantMap>

CellBroadcastTopicRange::CellBroadcastTopicRange()
    : from(0)
    , to(0)
{
}

CellBroadcastTopicRange::CellBroadcastTopicRange(int first, int last)
    : from(first)
    , to(last)
{
}

CellBroadcastTopicRangeList CellBroadcastTopics::parse(const QString &topics)
{
    CellBroadcastTopicRangeList ranges;
    const QString trimmed = topics.trimmed();
    if (trimmed.isEmpty()) {
        return ranges;
    }

    const QStringList parts = trimmed.split(QLatin1Char(','),
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
                                            Qt::SkipEmptyParts
#else
                                            QString::SkipEmptyParts
#endif
                                            );
    for (const QString &part : parts) {
        const QString range = part.trimmed();
        const int separator = range.indexOf(QLatin1Char('-'));
        bool ok = false;
        int first = 0;
        int last = 0;
        if (separator >= 0) {
            first = range.left(separator).toInt(&ok);
            if (!ok) {
                continue;
            }
            last = range.mid(separator + 1).toInt(&ok);
            if (!ok) {
                continue;
            }
        } else {
            first = last = range.toInt(&ok);
            if (!ok) {
                continue;
            }
        }
        if (first < 0 || last < first || last > 65535) {
            continue;
        }
        ranges.append(CellBroadcastTopicRange(first, last));
    }

    return normalize(ranges);
}

QString CellBroadcastTopics::format(const CellBroadcastTopicRangeList &ranges)
{
    QStringList parts;
    const CellBroadcastTopicRangeList normalized = normalize(ranges);
    for (const CellBroadcastTopicRange &range : normalized) {
        parts.append(rangeToString(range));
    }
    return parts.join(QLatin1Char(','));
}

QString CellBroadcastTopics::rangeToString(const CellBroadcastTopicRange &range)
{
    if (range.from == range.to) {
        return QString::number(range.from);
    }
    return QString::number(range.from) + QLatin1Char('-') + QString::number(range.to);
}

CellBroadcastTopicRangeList CellBroadcastTopics::normalize(const CellBroadcastTopicRangeList &ranges)
{
    CellBroadcastTopicRangeList sorted;
    for (const CellBroadcastTopicRange &range : ranges) {
        if (range.from <= range.to) {
            sorted.append(range);
        }
    }

    std::sort(sorted.begin(), sorted.end(), [](const CellBroadcastTopicRange &left,
                                               const CellBroadcastTopicRange &right) {
        return left.from == right.from ? left.to < right.to : left.from < right.from;
    });

    CellBroadcastTopicRangeList normalized;
    for (const CellBroadcastTopicRange &range : sorted) {
        if (!normalized.isEmpty() && range.from <= normalized.last().to + 1) {
            normalized.last().to = qMax(normalized.last().to, range.to);
        } else {
            normalized.append(range);
        }
    }
    return normalized;
}

CellBroadcastTopicRangeList CellBroadcastTopics::unite(const CellBroadcastTopicRangeList &left,
                                                       const CellBroadcastTopicRangeList &right)
{
    CellBroadcastTopicRangeList ranges = left;
    ranges.append(right);
    return normalize(ranges);
}

CellBroadcastTopicRangeList CellBroadcastTopics::subtract(const CellBroadcastTopicRangeList &ranges,
                                                          const CellBroadcastTopicRangeList &remove)
{
    CellBroadcastTopicRangeList result = normalize(ranges);
    const CellBroadcastTopicRangeList normalizedRemove = normalize(remove);

    for (const CellBroadcastTopicRange &removeRange : normalizedRemove) {
        CellBroadcastTopicRangeList next;
        for (const CellBroadcastTopicRange &range : result) {
            if (removeRange.to < range.from || removeRange.from > range.to) {
                next.append(range);
                continue;
            }
            if (removeRange.from > range.from) {
                next.append(CellBroadcastTopicRange(range.from, removeRange.from - 1));
            }
            if (removeRange.to < range.to) {
                next.append(CellBroadcastTopicRange(removeRange.to + 1, range.to));
            }
        }
        result = next;
    }

    return normalize(result);
}

bool CellBroadcastTopics::equals(const CellBroadcastTopicRangeList &left,
                                 const CellBroadcastTopicRangeList &right)
{
    const CellBroadcastTopicRangeList normalizedLeft = normalize(left);
    const CellBroadcastTopicRangeList normalizedRight = normalize(right);
    if (normalizedLeft.count() != normalizedRight.count()) {
        return false;
    }

    for (int i = 0; i < normalizedLeft.count(); ++i) {
        if (normalizedLeft.at(i).from != normalizedRight.at(i).from
                || normalizedLeft.at(i).to != normalizedRight.at(i).to) {
            return false;
        }
    }
    return true;
}

QVariantList CellBroadcastTopics::toVariantList(const CellBroadcastTopicRangeList &ranges)
{
    QVariantList values;
    const CellBroadcastTopicRangeList normalized = normalize(ranges);
    for (const CellBroadcastTopicRange &range : normalized) {
        QVariantMap value;
        value.insert(QStringLiteral("from"), range.from);
        value.insert(QStringLiteral("to"), range.to);
        value.insert(QStringLiteral("topics"), rangeToString(range));
        values.append(value);
    }
    return values;
}
