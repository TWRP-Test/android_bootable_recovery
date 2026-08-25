/*
        Copyright 2012 bigbiff/Dees_Troy TeamWin
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

#ifndef _GUI_HEADER
#define _GUI_HEADER

#include <stdio.h>

int gui_init();
int gui_loadResources();
int gui_loadCustomResources();
int gui_start();
int gui_startPage(const char* page_name, const int allow_comands, int stop_on_page_done);
// Run the independent LVGL GUI2 as a child process. Releases the minui
// display/input before exec so gui2 can take over DRM master, and restores
// them afterwards so the legacy GUI can continue. Returns the gui2 exit
// status, or -1 when it could not be started.
int gui_run_gui2();
void gui_print(const char *fmt, ...);
void gui_print_color(const char *color, const char *fmt, ...);
void gui_set_FILE(FILE* f);

void set_scale_values(float w, float h);
int scale_theme_x(int initial_x);
int scale_theme_y(int initial_y);
int scale_theme_min(int initial_value);
float get_scale_w();
float get_scale_h();

#endif  // _GUI_HEADER

