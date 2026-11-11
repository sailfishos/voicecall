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
#ifndef DTMF_H
#define DTMF_H

#include <QChar>

namespace Dtmf {

// The Telepathy DTMF event (0-9, * = 10, # = 11, A-D = 12-15) of a keypad
// character, or -1.
inline int event(QChar key)
{
    const ushort c = key.unicode();
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c == '*')
        return 10;
    if (c == '#')
        return 11;
    if (c >= 'A' && c <= 'D')
        return 12 + (c - 'A');
    if (c >= 'a' && c <= 'd')
        return 12 + (c - 'a');
    return -1;
}

}

#endif // DTMF_H
