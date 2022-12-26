// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2009-2022, Intel Corporation
// written by Steven Briscoe

#include <cstdlib>
#include <iostream>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <errno.h>
#include <sstream>
#include <exception>
#include <stdexcept>
#include <grp.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "../daemon/common.h"
#include "client.h"

namespace PCMDaemon {

	Client::Client()
	: pollIntervalMs_(0), shmIdLocation_(DEFAULT_SHM_ID_LOCATION), shmAttached_(false), lastUpdatedClientTsc_(0)
	{}

	void Client::setSharedMemoryIdLocation(const std::string& location)
	{
		if(shmAttached_)
		{
			throw std::runtime_error("Shared memory segment already attached. You must call this method before the .connect() method.");
		}
		shmIdLocation_ = location;
	}

	void Client::setPollInterval(int pollMs)
	{
		pollIntervalMs_ = pollMs;
	}

	void Client::connect()
	{
		setupSharedMemory();

		//Set last updated timestamp to avoid a detected change
		//when the client starts
		lastUpdatedClientTsc_ = sharedPCMState_->lastUpdateTscEnd;
	}

	PCMDaemon::SharedPCMState& Client::read()
	{
		if(pollIntervalMs_ <= 0)
		{
			throw std::runtime_error("The poll interval is not set or is negative.");
		}

		if(!shmAttached_)
		{
			throw std::runtime_error("Not attached to shared memory segment. Call .connect() method.");
		}

		while(true)
		{
			// Check client version matches daemon version
			if(strlen(sharedPCMState_->version) > 0 && strcmp(sharedPCMState_->version, VERSION) != 0)
			{
				std::cout << sharedPCMState_->lastUpdateTscEnd << " " << lastUpdatedClientTsc_ << "\n";
				std::stringstream ss;
				ss << "Out of date PCM daemon client. Client version: " << VERSION << " Daemon version: " << sharedPCMState_->version;

				throw std::runtime_error(ss.str());
			}

			if(countersHaveUpdated())
			{
				//There is new data
				lastUpdatedClientTsc_ = sharedPCMState_->lastUpdateTscEnd;

				return *sharedPCMState_;
			}
			else
			{
				//Nothing has changed since we last checked
				usleep(pollIntervalMs_ * 1000);
			}
		}
	}

	bool Client::countersHaveUpdated()
	{
		return lastUpdatedClientTsc_ != sharedPCMState_->lastUpdateTscEnd;
	}

	void Client::setupSharedMemory()
	{
		int sharedMemoryId;
		FILE *fp = fopen (shmIdLocation_.c_str(), "r");
		if (!fp)
		{
			std::cerr << "Failed to open to shared memory key location: " << shmIdLocation_ << "\n";
			exit(EXIT_FAILURE);
		}
		const int maxCharsToRead = 11;
		char readBuffer[maxCharsToRead + 1];
		std::fill((char*)readBuffer, ((char*)readBuffer) + sizeof(readBuffer), 0);
		const auto nread = fread(&readBuffer, maxCharsToRead, 1, fp);
		if (nread == 0 && feof(fp) == 0)
		{
			fclose (fp);
			std::stringstream ss;
			ss << "fread failed for " << shmIdLocation_;
			throw std::runtime_error(ss.str());
		}
		fclose (fp);
		assert(nread <= maxCharsToRead);

		sharedMemoryId = atoi(readBuffer);

		sharedPCMState_ = (PCMDaemon::SharedPCMState*)shmat(sharedMemoryId, NULL, 0);
		if (sharedPCMState_ == (void *)-1)
		{
			std::stringstream ss;
			ss << "Failed to attach shared memory segment (errno=" << errno << ") " << strerror(errno);

			throw std::runtime_error(ss.str());
		}

		shmAttached_ = true;
	}

}
