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
#ifndef HEARINGAIDPREFERENCE_H
#define HEARINGAIDPREFERENCE_H

#include <MDConfItem>
#include <QObject>

class HearingAidPreference : public QObject
{
public:
    explicit HearingAidPreference(QObject *parent);

private:
    void request();
    void apply();

    MDConfItem m_enabled;
    bool m_pending;
    bool m_dirty;
};

#endif
