/*
 * Copyright (C) 2014 TeamWin - bigbiff and Dees_Troy mtp database conversion to C++
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *	  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */


#include <string>

#include <fcntl.h>
#include <pthread.h>
#include <sys/wait.h>
#include <unistd.h>

#include "MtpDebug.h"
#include "TwrpMtp.hpp"
#include "TwrpMtpServer.hpp"

TwrpMtp::TwrpMtp(int debug_enabled) {
	if (debug_enabled)
		MtpDebug::enableDebug();
	mtpstorages = new storages;
	mtp_read_pipe = -1;
}

int TwrpMtp::start(void) {
	MTPI("Starting MTP\n");
	TwrpMtpServer *mtp = new TwrpMtpServer();
	mtp->set_storages(mtpstorages);
	mtp->set_device_info();
	mtp->set_read_pipe(mtp_read_pipe);
	mtp->start();
	return 0;
}

pthread_t TwrpMtp::threadserver(void) {
	pthread_t thread;
	ThreadPtr mtpptr = &TwrpMtp::start;
	PThreadPtr p = *(PThreadPtr*)&mtpptr;
	pthread_create(&thread, NULL, p, this);
	return thread;
}

pid_t TwrpMtp::forkserver(int mtppipe[2]) {
	pid_t pid;
	if ((pid = fork()) == -1) {
		MTPE("MTP fork failed.\n");
		return 0;
	}
	if (pid == 0) {
		// Child process
		close(mtppipe[1]); // Child closes write side
		mtp_read_pipe = mtppipe[0];
		start();
		MTPD("MTP child process exited.\n");
		close(mtppipe[0]);
		_exit(0);
	} else {
		MTPD("MTP child PID: %d\n", pid);
		return pid;
	}
	return 0;
}

void TwrpMtp::addStorage(std::string display, std::string path, int mtpid, uint64_t maxFileSize) {
	s = new storage;
	s->display = display;
	s->mount = path;
	s->mtpid = mtpid;
	s->maxFileSize = maxFileSize;
	MTPD("TwrpMtp mtpid: %d\n", s->mtpid);
	mtpstorages->push_back(s);
}
