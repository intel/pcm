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

//Test program for PCM Daemon client

#include <iostream>
#include <iomanip>
#include <cstdlib>

#include "client.h"

void printTitle(std::string title)
{
	std::cout << std::setw(26) << std::left << title;
}

int main(int argc, char *argv[])
{
	PCMDaemon::Client client;
	// client.setSharedMemoryIdLocation("/tmp/test-file");
	client.connect();
	client.setPollInterval(atoi(argv[1]));

	int coutPrecision = 2;

	while(true)
	{
		PCMDaemon::SharedPCMState& state = client.read();
		PCMDaemon::SharedPCMCounters& counters = state.pcm;

		std::cout << "\n----- Something changed -----\n\n";

// 		Display internal metrics
		printTitle("Last updated TSC");
		std::cout << state.lastUpdateTscEnd << "\n";

		printTitle("Timestamp");
		std::cout << state.timestamp << "\n";

		printTitle("Cycles to get counters");
		std::cout << state.cyclesToGetPCMState << "\n";

		printTitle("Poll interval (ms)");
		std::cout << state.pollMs << "\n";

		std::cout << "\n\n";

//		Display system counters
		printTitle("Num. of cores");
		std::cout << counters.system.numOfCores << "\n";
		
		printTitle("Num. of online cores");
		std::cout << counters.system.numOfOnlineCores << "\n";
		
		printTitle("Num. of sockets");
		std::cout << counters.system.numOfSockets << "\n";
		
		printTitle("Num. of online sockets");
		std::cout << counters.system.numOfOnlineSockets << "\n";
		
		printTitle("QPI links per socket");
		std::cout << counters.system.numOfQPILinksPerSocket << "\n";

		std::cout << "\n\n";

//		Display core counters
		printTitle("Core ID");
		for(PCMDaemon::uint32 i = 0; i < counters.system.numOfOnlineCores; ++i)
		{
			std::cout << std::setprecision(coutPrecision) << counters.core.cores[i].coreId << " ";
		}
		std::cout << "\n";

		printTitle("Socket ID");
		for(PCMDaemon::uint32 i = 0; i < counters.system.numOfOnlineCores; ++i)
		{
			std::cout << std::setprecision(coutPrecision) << counters.core.cores[i].socketId << " ";
		}
		std::cout << "\n";

		printTitle("IPC");
		for(PCMDaemon::uint32 i = 0; i < counters.system.numOfOnlineCores; ++i)
		{
			std::cout << std::setprecision(coutPrecision) << counters.core.cores[i].instructionsPerCycle << " ";
		}
		std::cout << "\n";

		printTitle("Cycles");
		for(PCMDaemon::uint32 i = 0; i < counters.system.numOfOnlineCores; ++i)
		{
			std::cout << std::setprecision(coutPrecision) << counters.core.cores[i].cycles << " ";
		}
		std::cout << "\n";

		printTitle("Inst. Ret.");
		for(PCMDaemon::uint32 i = 0; i < counters.system.numOfOnlineCores; ++i)
		{
			std::cout << std::setprecision(coutPrecision) << counters.core.cores[i].instructionsRetired << " ";
		}
		std::cout << "\n";

		printTitle("Exec usg.");
		for(PCMDaemon::uint32 i = 0; i < counters.system.numOfOnlineCores; ++i)
		{
			std::cout << std::setprecision(coutPrecision) << counters.core.cores[i].execUsage << " ";
		}
		std::cout << "\n";

		printTitle("Rela. Freq.");
		for(PCMDaemon::uint32 i = 0; i < counters.system.numOfOnlineCores; ++i)
		{
			std::cout << std::setprecision(coutPrecision) << counters.core.cores[i].relativeFrequency << " ";
		}
		std::cout << "\n";

		printTitle("Active Rela. Freq");
		for(PCMDaemon::uint32 i = 0; i < counters.system.numOfOnlineCores; ++i)
		{
			std::cout << std::setprecision(coutPrecision) << counters.core.cores[i].activeRelativeFrequency << " ";
		}
		std::cout << "\n";

		printTitle("L3 C Miss");
		for(PCMDaemon::uint32 i = 0; i < counters.system.numOfOnlineCores; ++i)
		{
			std::cout << std::setprecision(coutPrecision) << counters.core.cores[i].l3CacheMisses << " ";
		}
		std::cout << "\n";

		printTitle("L3 C Reference");
		for(PCMDaemon::uint32 i = 0; i < counters.system.numOfOnlineCores; ++i)
		{
			std::cout << std::setprecision(coutPrecision) << counters.core.cores[i].l3CacheReference << " ";
		}
		std::cout << "\n";

		printTitle("L2 C Miss");
		for(PCMDaemon::uint32 i = 0; i < counters.system.numOfOnlineCores; ++i)
		{
			std::cout << std::setprecision(coutPrecision) << counters.core.cores[i].l2CacheMisses << " ";
		}
		std::cout << "\n";

		printTitle("L3 Hit Ratio");
		for(PCMDaemon::uint32 i = 0; i < counters.system.numOfOnlineCores; ++i)
		{
			std::cout << std::setprecision(coutPrecision) << counters.core.cores[i].l3CacheHitRatio << " ";
		}
		std::cout << "\n";

		printTitle("L2 Hit Ratio");
		for(PCMDaemon::uint32 i = 0; i < counters.system.numOfOnlineCores; ++i)
		{
			std::cout << std::setprecision(coutPrecision) << counters.core.cores[i].l2CacheHitRatio << " ";
		}
		std::cout << "\n";

		printTitle("L3 C MPI");
		for(PCMDaemon::uint32 i = 0; i < counters.system.numOfOnlineCores; ++i)
		{
			std::cout << std::setprecision(coutPrecision) << counters.core.cores[i].l3CacheMPI << " ";
		}
		std::cout << "\n";

		printTitle("L2 C MPI");
		for(PCMDaemon::uint32 i = 0; i < counters.system.numOfOnlineCores; ++i)
		{
			std::cout << std::setprecision(coutPrecision) << counters.core.cores[i].l2CacheMPI << " ";
		}
		std::cout << "\n";

		printTitle("L3 Occu. Avail.");
		for(PCMDaemon::uint32 i = 0; i < counters.system.numOfOnlineCores; ++i)
		{
			std::cout << std::setprecision(coutPrecision) << counters.core.cores[i].l3CacheOccupancyAvailable << " ";
		}
		std::cout << "\n";

		printTitle("L3 Occu.");
		for(PCMDaemon::uint32 i = 0; i < counters.system.numOfOnlineCores; ++i)
		{
			std::cout << std::setprecision(coutPrecision) << counters.core.cores[i].l3CacheOccupancy << " ";
		}
		std::cout << "\n";

		printTitle("L. Mem. BW Avail.");
		for(PCMDaemon::uint32 i = 0; i < counters.system.numOfOnlineCores; ++i)
		{
			std::cout << std::setprecision(coutPrecision) << counters.core.cores[i].localMemoryBWAvailable << " ";
		}
		std::cout << "\n";

		printTitle("L. Mem. BW");
		for(PCMDaemon::uint32 i = 0; i < counters.system.numOfOnlineCores; ++i)
		{
			std::cout << std::setprecision(coutPrecision) << counters.core.cores[i].localMemoryBW << " ";
		}
		std::cout << "\n";

		printTitle("R. Mem. BW Avail.");
		for(PCMDaemon::uint32 i = 0; i < counters.system.numOfOnlineCores; ++i)
		{
			std::cout << std::setprecision(coutPrecision) << counters.core.cores[i].remoteMemoryBWAvailable << " ";
		}
		std::cout << "\n";

		printTitle("R. Mem. BW");
		for(PCMDaemon::uint32 i = 0; i < counters.system.numOfOnlineCores; ++i)
		{
			std::cout << std::setprecision(coutPrecision) << counters.core.cores[i].remoteMemoryBW << " ";
		}
		std::cout << "\n";

		printTitle("L. Mem. Accesses");
		for(PCMDaemon::uint32 i = 0; i < counters.system.numOfOnlineCores; ++i)
		{
			std::cout << std::setprecision(coutPrecision) << counters.core.cores[i].localMemoryAccesses << " ";
		}
		std::cout << "\n";

		printTitle("R. Mem. Accesses");
		for(PCMDaemon::uint32 i = 0; i < counters.system.numOfOnlineCores; ++i)
		{
			std::cout << std::setprecision(coutPrecision) << counters.core.cores[i].remoteMemoryAccesses << " ";
		}
		std::cout << "\n";

		printTitle("Thermal headroom");
		for(PCMDaemon::uint32 i = 0; i < counters.system.numOfOnlineCores; ++i)
		{
			std::cout << std::setprecision(coutPrecision) << counters.core.cores[i].thermalHeadroom << " ";
		}
		std::cout << "\n";

		std::cout << "\n\n";

//		Display memory counters
		printTitle("Mem Read p/Sock.");
		for(PCMDaemon::uint32 i = 0; i < counters.system.numOfOnlineSockets; ++i)
		{
			std::cout << std::setprecision(coutPrecision) << counters.memory.sockets[i].read << " ";
		}
		std::cout << "\n";

		printTitle("Mem Write p/Sock.");
		for(PCMDaemon::uint32 i = 0; i < counters.system.numOfOnlineSockets; ++i)
		{
			std::cout << std::setprecision(coutPrecision) << counters.memory.sockets[i].write << " ";
		}
		std::cout << "\n";

		printTitle("Mem Part. p/Sock.");
		for(PCMDaemon::uint32 i = 0; i < counters.system.numOfOnlineSockets; ++i)
		{
			std::cout << std::setprecision(coutPrecision) << counters.memory.sockets[i].partialWrite << " ";
		}
		std::cout << "\n";

		printTitle("Mem Total p/Sock.");
		for(PCMDaemon::uint32 i = 0; i < counters.system.numOfOnlineSockets; ++i)
		{
			std::cout << std::setprecision(coutPrecision) << counters.memory.sockets[i].total << " ";
		}
		std::cout << "\n";

		printTitle("Mem Read Sys.");
		std::cout << std::setprecision(coutPrecision) << counters.memory.system.total << " ";
		std::cout << "\n";

		printTitle("Mem Write Sys.");
		std::cout << std::setprecision(coutPrecision) << counters.memory.system.write << " ";
		std::cout << "\n";

		printTitle("Mem Total Sys.");
		std::cout << std::setprecision(coutPrecision) << counters.memory.system.total << " ";
		std::cout << "\n";

		printTitle("Mem Energy Avail.");
		std::cout << std::setprecision(coutPrecision) << counters.memory.dramEnergyMetricsAvailable << " ";
		std::cout << "\n";

		if(counters.memory.dramEnergyMetricsAvailable)
		{
			printTitle("Mem Energy p/Sock");
			for(PCMDaemon::uint32 i = 0; i < counters.system.numOfOnlineSockets; ++i)
			{
				std::cout << std::setprecision(coutPrecision) << counters.memory.sockets[i].dramEnergy << " ";
			}
			std::cout << "\n";
		}

		std::cout << "\n\n";

//		Display QPI counters
		printTitle("QPI in. Avail.");
		std::cout << std::setprecision(coutPrecision) << counters.qpi.incomingQPITrafficMetricsAvailable << " ";
		std::cout << "\n";

		if(counters.qpi.incomingQPITrafficMetricsAvailable)
		{
			printTitle("QPI No. of Links");
			std::cout << std::setprecision(coutPrecision) << counters.system.numOfQPILinksPerSocket << "\n";

			printTitle("QPI in. p/Sock");
			for(PCMDaemon::uint32 i = 0; i < counters.system.numOfOnlineSockets; ++i)
			{
				std::cout << std::setprecision(coutPrecision) << counters.qpi.incoming[i].total << " ";
			}
			std::cout << "\n";

			printTitle("QPI in. p/Link/Sock");
			for(PCMDaemon::uint32 i = 0; i < counters.system.numOfOnlineSockets; ++i)
			{
				std::cout << "Socket: " << i << " (bytes)\t\t";
				for(PCMDaemon::uint32 l = 0; l < counters.system.numOfQPILinksPerSocket; ++l)
				{
					std::cout << std::setw(12) << std::left << std::setprecision(coutPrecision) << counters.qpi.incoming[i].links[l].bytes << " ";
				}
				std::cout << "\n";
				printTitle("");

				std::cout << "Socket: " << i << " (utilization)\t";
				for(PCMDaemon::uint32 l = 0; l < counters.system.numOfQPILinksPerSocket; ++l)
				{
					std::cout << std::setw(12) << std::left << std::setprecision(coutPrecision) << counters.qpi.incoming[i].links[l].utilization << " ";
				}
				std::cout << "\n";
				printTitle("");
			}
			std::cout << "\n";

			printTitle("QPI in. Total");
			std::cout << std::setprecision(coutPrecision) << counters.qpi.incomingTotal << " ";
			std::cout << "\n\n";
		}

		printTitle("QPI out. Avail.");
		std::cout << std::setprecision(coutPrecision) << counters.qpi.outgoingQPITrafficMetricsAvailable << " ";
		std::cout << "\n";

		if(counters.qpi.outgoingQPITrafficMetricsAvailable)
		{
			printTitle("QPI No. of Links");
			std::cout << std::setprecision(coutPrecision) << counters.system.numOfQPILinksPerSocket << "\n";

			printTitle("QPI out. p/Sock");
			for(PCMDaemon::uint32 i = 0; i < counters.system.numOfOnlineSockets; ++i)
			{
				std::cout << std::setprecision(coutPrecision) << counters.qpi.outgoing[i].total << " ";
			}
			std::cout << "\n";

			printTitle("QPI out. p/Link/Sock");
			for(PCMDaemon::uint32 i = 0; i < counters.system.numOfOnlineSockets; ++i)
			{
				std::cout << "Socket: " << i << " (bytes)\t\t";
				for(PCMDaemon::uint32 l = 0; l < counters.system.numOfQPILinksPerSocket; ++l)
				{
					std::cout << std::setw(12) << std::left << std::setprecision(coutPrecision) << counters.qpi.outgoing[i].links[l].bytes << " ";
				}
				std::cout << "\n";
				printTitle("");

				std::cout << "Socket: " << i << " (utilization)\t";
				for(PCMDaemon::uint32 l = 0; l < counters.system.numOfQPILinksPerSocket; ++l)
				{
					std::cout << std::setw(12) << std::left << std::setprecision(coutPrecision) << counters.qpi.outgoing[i].links[l].utilization << " ";
				}
				std::cout << "\n";
				printTitle("");
			}
			std::cout << "\n";

			printTitle("QPI out. Total");
			std::cout << std::setprecision(coutPrecision) << counters.qpi.outgoingTotal << " ";
			std::cout << "\n";
		}
                std::cout << std::flush;
	}

	return 1;
}
