// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2009-2017, Intel Corporation
// written by Steven Briscoe

#include <string>

#include "../daemon/common.h"

#ifndef CLIENT_H_
#define CLIENT_H_

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
