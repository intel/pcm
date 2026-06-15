// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2009-2022, Intel Corporation
// written by Steven Briscoe

#include <sys/types.h>
#include <string>
#include <grp.h>


#ifndef CLIENT_H_
#define CLIENT_H_

#include "../daemon/common.h"

namespace PCMDaemon {

	class Client {
	public:
		Client();
		void setSharedMemoryIdLocation(const std::string& location);
		void setPollInterval(int pollMs);
		void connect();
		PCMDaemon::SharedPCMState& read();
		bool countersHaveUpdated();
	private:
		void setupSharedMemory();

		int pollIntervalMs_;
		std::string shmIdLocation_;
		bool shmAttached_;
		PCMDaemon::SharedPCMState* sharedPCMState_ = nullptr;
		PCMDaemon::uint64 lastUpdatedClientTsc_;
	};

}


#endif /* CLIENT_H_ */
