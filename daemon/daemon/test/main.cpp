#include <stdio.h>
#include <cstdlib>
#include <cstdint>

#include "../common.h"

#define ALIGNMENT 64

void checkAlignment(char const * debugMessage, void* ptr)
{
	printf("Checking: %-20s\t\t", debugMessage);
	uint64_t currentAlignment = (uint64_t)ptr % ALIGNMENT;
	if(currentAlignment != 0)
	{
		printf("Failed\n");
		printf("Current alignment: %lu\n\n", currentAlignment);
		exit(EXIT_FAILURE);
	}
	else
	{
		printf("Passed\n");
	}
}

int main()
{
    printf("Testing alignment\n\n");

    PCMDaemon::SharedPCMState* pcmState = (PCMDaemon::SharedPCMState*)aligned_alloc(ALIGNMENT, sizeof(PCMDaemon::SharedPCMState));

    checkAlignment("pcmState", pcmState);

    checkAlignment("pcm", &pcmState->pcm);

    checkAlignment("pcm core", &pcmState->pcm.core);
    checkAlignment("pcm memory", &pcmState->pcm.memory);
    checkAlignment("pcm qpi", &pcmState->pcm.qpi);

    for(uint32_t i(0); i < MAX_CPU_CORES; ++i)
    {
    	checkAlignment("pcm core cores", &pcmState->pcm.core.cores[i]);
    }

    checkAlignment("pcm core energyUsed", &pcmState->pcm.core.energyUsedBySockets);

    for(uint32_t i(0); i < MAX_SOCKETS; ++i)
    {
    	checkAlignment("pcm memory sockets", &pcmState->pcm.memory.sockets[i]);
    }

    checkAlignment("pcm memory dramEnergy", &pcmState->pcm.memory.dramEnergyForSockets);

    for(uint32_t i(0); i < MAX_SOCKETS; ++i)
    {
    	checkAlignment("pcm qpi incoming", &pcmState->pcm.qpi.incoming[i]);
    }
    
    for(uint32_t i(0); i < MAX_SOCKETS; ++i)
    {
    	for(uint32_t j(0); j < QPI_MAX_LINKS; ++j)
    	{
    		checkAlignment("pcm qpi incoming links", &pcmState->pcm.qpi.incoming[i].links[j]);
    	}
    }

    for(uint32_t i(0); i < MAX_SOCKETS; ++i)
    {
    	checkAlignment("pcm qpi outgoing", &pcmState->pcm.qpi.outgoing[i]);
    }

    for(uint32_t i(0); i < MAX_SOCKETS; ++i)
    {
    	for(uint32_t j(0); j < QPI_MAX_LINKS; ++j)
    	{
    		checkAlignment("pcm qpi outgoing links", &pcmState->pcm.qpi.outgoing[i].links[j]);
    	}
    }

    free(pcmState);

    printf("\n------ All passed ------\n\n");

    return EXIT_SUCCESS;
}