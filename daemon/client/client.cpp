/*
   Copyright (c) 2009-2017, Intel Corporation
   All rights reserved.

   Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

 * Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
 * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
 * Neither the name of Intel Corporation nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
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
		int maxCharsToRead = 11;
		char readBuffer[maxCharsToRead];
		fread(&readBuffer, maxCharsToRead, 1, fp);
		fclose (fp);

		sharedMemoryId = atoi(readBuffer);

		sharedPCMState_ = (PCMDaemon::SharedPCMState*)shmat(sharedMemoryId, NULL, 0);
		if (sharedPCMState_ == (void *)-1)
		{
			std::stringstream ss;
			ss << "Failed to attach shared memory segment (errno=" << errno << ")";

			throw std::runtime_error(ss.str());
		}

		shmAttached_ = true;
	}

}
