/*
	Copyright 2026 TeamWin
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

#ifndef TWRP_GUI_TERMINAL_HPP
#define TWRP_GUI_TERMINAL_HPP

#include <cstddef>
#include <string>

// terminal.cpp owns one pty and one text buffer, and says so itself: any number
// of front ends may share a single engine. These are how a front end outside
// gui/ reaches it, so a second UI does not start a second shell.
namespace twrp_terminal {

// Starts the shell if it is not already running.
void start();
bool running();
void stop();

// In characters. The engine wraps and scrolls against this.
void set_size(int columns, int rows);

// Reads whatever the shell has produced. gui/gui.cpp calls this from its own
// loop; a front end that does not run that loop has to call it itself.
void pump();

// Changes whenever the buffer changed, so a front end can skip redrawing.
int update_counter();

size_t line_count();
std::string line(size_t index);

void input_char(int codepoint);
void input_key(int key);

}  // namespace twrp_terminal

#endif  // TWRP_GUI_TERMINAL_HPP
