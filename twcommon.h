/*
    Copyright 2024 TeamWin
    This file is part of TWRP/TeamWin Recovery Project.

    TWRP is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    TWRP is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with TWRP.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef TWCOMMON_HPP
#define TWCOMMON_HPP

#include "gui/gui.hpp"
#define LOGERR(...) gui_print_color("error", "E:" __VA_ARGS__)
#define LOGINFO(...) fprintf(stdout, "I:" __VA_ARGS__)

#define STRINGIFY(...) #__VA_ARGS__
#define EXPAND(x) STRINGIFY(x)

#endif  // TWCOMMON_HPP
