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
#ifndef CELLBROADCASTGEOMETRY_H
#define CELLBROADCASTGEOMETRY_H

#include <QString>

class CellBroadcastGeometry
{
public:
    enum Evaluation {
        Invalid,
        Outside,
        Ambiguous,
        Inside
    };

    static Evaluation evaluate(const QString &geometries,
                               double latitude,
                               double longitude,
                               double accuracyMeters);
};

#endif
