/*
 * Copyright (C) 2010 The Android Open Source Project
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
 *
 * Copyright (C) 2014 TeamWin - bigbiff and Dees_Troy mtp database conversion to C++
 */

#ifndef TWRP_MTP_SERVER_HPP
#define TWRP_MTP_SERVER_HPP

#include <string>
#include <vector>

#include "MtpServer.h"
#include "MtpStorage.h"
#include "MtpStringBuffer.h"

typedef struct Storage {
	std::string display;
	std::string mount;
	int mtpid;
	uint64_t maxFileSize;
} storage;

typedef std::vector<storage*> storages;

struct mtp_info {
	MtpStringBuffer deviceInfoManufacturer;
	MtpStringBuffer deviceInfoModel;
	MtpStringBuffer deviceInfoDeviceVersion;
	MtpStringBuffer deviceInfoSerialNumber;
};

class TwrpMtpServer {
	public:
		void start();
		void cleanup();
		void send_object_added(int handle);
		void send_object_removed(int handle);
		void add_storage();
		void remove_storage(int storageId);
		void set_storages(storages* mtpstorages);
		void set_read_pipe(int pipe);
		storages *stores;
		struct mtp_info mtpinfo;
		void set_device_info();

	private:
		typedef int (TwrpMtpServer::*ThreadPtr)(void);
		typedef void* (*PThreadPtr)(void *);
		int mtppipe_thread(void);
		bool usePtp;
		MtpServer* server;
		MtpServer* refserver;
		int mtp_read_pipe;
};
#endif
