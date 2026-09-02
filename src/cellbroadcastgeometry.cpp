/*
 * This file is a part of the Voice Call Manager project
 *
 * Copyright (C) 2026 Jolla Mobile Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include "cellbroadcastgeometry.h"

#include <QList>
#include <QtMath>

namespace {

const double EarthRadiusMeters = 6371000.0;

struct Point {
    double latitude;
    double longitude;
};

double longitudeDelta(double from, double to)
{
    double delta = to - from;
    while (delta > 180.0) {
        delta -= 360.0;
    }
    while (delta < -180.0) {
        delta += 360.0;
    }
    return delta;
}

bool parsePoint(const QString &text, Point *point)
{
    const QStringList values = text.split(QLatin1Char(','));
    if (values.count() != 2) {
        return false;
    }
    bool latitudeOk = false;
    bool longitudeOk = false;
    const double latitude = values.at(0).toDouble(&latitudeOk);
    const double longitude = values.at(1).toDouble(&longitudeOk);
    if (!latitudeOk || !longitudeOk || latitude < -90.0 || latitude > 90.0
            || longitude < -180.0 || longitude > 180.0) {
        return false;
    }
    point->latitude = latitude;
    point->longitude = longitude;
    return true;
}

double distance(const Point &a, const Point &b)
{
    const double latitude1 = qDegreesToRadians(a.latitude);
    const double latitude2 = qDegreesToRadians(b.latitude);
    const double latitudeDelta = latitude2 - latitude1;
    const double lonDelta = qDegreesToRadians(longitudeDelta(a.longitude, b.longitude));
    const double value = qSin(latitudeDelta / 2.0) * qSin(latitudeDelta / 2.0)
            + qCos(latitude1) * qCos(latitude2)
            * qSin(lonDelta / 2.0) * qSin(lonDelta / 2.0);
    return EarthRadiusMeters * 2.0 * qAtan2(qSqrt(value), qSqrt(1.0 - value));
}

struct LocalPoint {
    double x;
    double y;
};

LocalPoint localPoint(const Point &origin, const Point &point)
{
    const double meanLatitude = qDegreesToRadians((origin.latitude + point.latitude) / 2.0);
    LocalPoint result;
    result.x = qDegreesToRadians(longitudeDelta(origin.longitude, point.longitude))
            * EarthRadiusMeters * qCos(meanLatitude);
    result.y = qDegreesToRadians(point.latitude - origin.latitude) * EarthRadiusMeters;
    return result;
}

double segmentDistance(const LocalPoint &a, const LocalPoint &b)
{
    const double lengthSquared = (b.x - a.x) * (b.x - a.x)
            + (b.y - a.y) * (b.y - a.y);
    if (lengthSquared == 0.0) {
        return qSqrt(a.x * a.x + a.y * a.y);
    }
    const double projection = qBound(0.0,
                                     -(a.x * (b.x - a.x) + a.y * (b.y - a.y))
                                     / lengthSquared,
                                     1.0);
    const double x = a.x + projection * (b.x - a.x);
    const double y = a.y + projection * (b.y - a.y);
    return qSqrt(x * x + y * y);
}

CellBroadcastGeometry::Evaluation evaluateCircle(const QStringList &parts,
                                                 const Point &location,
                                                 double accuracyMeters)
{
    if (parts.count() != 3) {
        return CellBroadcastGeometry::Invalid;
    }
    Point center;
    bool radiusOk = false;
    const double radius = parts.at(2).toDouble(&radiusOk);
    if (!parsePoint(parts.at(1), &center) || !radiusOk || radius < 0.0) {
        return CellBroadcastGeometry::Invalid;
    }
    const double centerDistance = distance(center, location);
    if (centerDistance <= radius) {
        return CellBroadcastGeometry::Inside;
    }
    return centerDistance - radius <= accuracyMeters
            ? CellBroadcastGeometry::Ambiguous : CellBroadcastGeometry::Outside;
}

CellBroadcastGeometry::Evaluation evaluatePolygon(const QStringList &parts,
                                                  const Point &location,
                                                  double accuracyMeters)
{
    if (parts.count() < 4) {
        return CellBroadcastGeometry::Invalid;
    }
    QList<LocalPoint> points;
    for (int i = 1; i < parts.count(); ++i) {
        Point point;
        if (!parsePoint(parts.at(i), &point)) {
            return CellBroadcastGeometry::Invalid;
        }
        points.append(localPoint(location, point));
    }

    bool inside = false;
    double boundaryDistance = -1.0;
    for (int i = 0, previous = points.count() - 1; i < points.count(); previous = i++) {
        const LocalPoint &a = points.at(previous);
        const LocalPoint &b = points.at(i);
        if ((a.y > 0.0) != (b.y > 0.0)) {
            const double crossing = a.x + (b.x - a.x) * (-a.y) / (b.y - a.y);
            if (crossing > 0.0) {
                inside = !inside;
            }
        }
        const double edgeDistance = segmentDistance(a, b);
        if (boundaryDistance < 0.0 || edgeDistance < boundaryDistance) {
            boundaryDistance = edgeDistance;
        }
    }
    if (inside || boundaryDistance < 0.01) {
        return CellBroadcastGeometry::Inside;
    }
    return boundaryDistance <= accuracyMeters
            ? CellBroadcastGeometry::Ambiguous : CellBroadcastGeometry::Outside;
}

} // namespace

CellBroadcastGeometry::Evaluation CellBroadcastGeometry::evaluate(
        const QString &geometries, double latitude, double longitude,
        double accuracyMeters)
{
    Point location = { latitude, longitude };
    if (latitude < -90.0 || latitude > 90.0 || longitude < -180.0
            || longitude > 180.0 || accuracyMeters < 0.0) {
        return Invalid;
    }

    bool validGeometry = false;
    bool ambiguous = false;
    const QStringList shapes = geometries.split(QLatin1Char(';'), QString::SkipEmptyParts);
    for (const QString &shape : shapes) {
        const QStringList parts = shape.split(QLatin1Char('|'));
        Evaluation evaluation = Invalid;
        if (parts.value(0) == QLatin1String("circle")) {
            evaluation = evaluateCircle(parts, location, accuracyMeters);
        } else if (parts.value(0) == QLatin1String("polygon")) {
            evaluation = evaluatePolygon(parts, location, accuracyMeters);
        }
        if (evaluation == Invalid) {
            return Invalid;
        }
        validGeometry = true;
        if (evaluation == Inside) {
            return Inside;
        }
        if (evaluation == Ambiguous) {
            ambiguous = true;
        }
    }
    if (!validGeometry) {
        return Invalid;
    }
    return ambiguous ? Ambiguous : Outside;
}
