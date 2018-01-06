/*
 * Copyright (C) 2018 Olzhas Rakhimov
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/// @file
/// Macro helpers to declare/define Qt Signal with Verdigris.
///
/// @pre <verdigris/wobjectdef.h> is included in the header file.

#pragma once

/// Wrapper to declare and define signal in one line.
///
/// This is less confusing for clang-format and other tools.
///
/// Macros mangled by the number of signal parameters.
/// @{
#define GUI_SIGNAL(NAME) void NAME() W_SIGNAL(NAME)
#define GUI_SIGNAL1(NAME, TYPE, PARAM)                                         \
    void NAME(TYPE PARAM) W_SIGNAL(NAME, (TYPE), PARAM)
#define GUI_SIGNAL2(NAME, TYPE1, PARAM1, TYPE2, PARAM2)                        \
    void NAME(TYPE1 PARAM1, TYPE2 PARAM2)                                      \
        W_SIGNAL(NAME, (TYPE1, TYPE2), PARAM1, PARAM2)
/// @}
