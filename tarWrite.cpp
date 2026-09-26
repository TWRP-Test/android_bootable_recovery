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

#include "tarWrite.hpp"

#include <android-base/file.h>
#include <cstdint>
#include <cstring>
#include <unistd.h>
#include <vector>

#include "libtar/libtar.h"
#include "twcommon.h"

namespace {
// File-scope state (tartype_t carries no closure pointer for the libtar callbacks).
bool flush_pending = false;
int eot_count = -1;
std::vector<unsigned char> write_buffer;
size_t buffer_size = 4096;
size_t buffer_loc = 0;
int buffer_status = 0;  // 0: none, 1: buffered, 2: flush requested
int prog_pipe = -1;
constexpr uint64_t kProgressSize = T_BLOCKSIZE;
}  // namespace

void reinit_libtar_buffer() {
	flush_pending = false;
	eot_count = -1;
	buffer_loc = 0;
	buffer_status = 1;
}

void init_libtar_buffer(unsigned new_buff_size, int pipe_fd) {
	if (new_buff_size != 0)
		buffer_size = new_buff_size;
	reinit_libtar_buffer();
	write_buffer.assign(buffer_size, 0);
	prog_pipe = pipe_fd;
}

void free_libtar_buffer() {
	if (buffer_status > 0)
		write_buffer.clear();
	buffer_status = 0;
	prog_pipe = -1;
}

ssize_t write_libtar_buffer(const int fd, const void* buffer, const size_t size) {
	if (!flush_pending) {
		std::memcpy(write_buffer.data() + buffer_loc, buffer, size);
		buffer_loc += size;
		if (eot_count >= 0 && eot_count < 2)
			eot_count++;
		// libtar appends 2 blank EOT blocks at EOF; flush once both arrive.
		if (buffer_loc >= buffer_size || eot_count >= 2)
			flush_pending = true;
	}
	if (flush_pending) {
		flush_pending = false;
		if (buffer_loc == 0)
			return 0;
		if (!android::base::WriteFully(fd, write_buffer.data(), buffer_loc)) {
			LOGERR("Error writing tar file!\n");
			buffer_loc = 0;
			return -1;
		}
		const uint64_t fs = buffer_loc;
		write(prog_pipe, &fs, sizeof(fs));
		buffer_loc = 0;
		return size;
	}
	return size;
}

void flush_libtar_buffer(int) {
	eot_count = 0;
	if (buffer_status)
		buffer_status = 2;
}

void init_libtar_no_buffer(int pipe_fd) {
	buffer_size = T_BLOCKSIZE;
	prog_pipe = pipe_fd;
	buffer_status = 0;
}

ssize_t write_libtar_no_buffer(int fd, const void* buffer, size_t size) {
	write(prog_pipe, &kProgressSize, sizeof(kProgressSize));
	if (!android::base::WriteFully(fd, buffer, size))
		return -1;
	return size;
}
