#include <stdio.h>
#include <dlfcn.h>
#include <stdint.h>

int pcm_getcpu()
{
    int id = -1;
    asm volatile (
       "rdtscp\n\t"
       "mov %%ecx, %0\n\t":
       "=r" (id) :: "%rax", "%rcx", "%rdx");
    // processor ID is in ECX: https://www.felixcloutier.com/x86/rdtscp
    return id;
}

struct {
	int (*pcm_c_build_core_event)(uint8_t id, const char * argv);
	int (*pcm_c_init)();
	void (*pcm_c_start)();
	void (*pcm_c_stop)();
	uint64_t (*pcm_c_get_cycles)(uint32_t core_id);
	uint64_t (*pcm_c_get_instr)(uint32_t core_id);
	uint64_t (*pcm_c_get_core_event)(uint32_t core_id, uint32_t event_id);
} PCM;

#ifndef PCM_DYNAMIC_LIB
/* Library functions declaration (instead of .h file) */
int pcm_c_build_core_event(uint8_t, const char *);
int pcm_c_init();
void pcm_c_start();
void pcm_c_stop();
uint64_t pcm_c_get_cycles(uint32_t);
uint64_t pcm_c_get_instr(uint32_t);
uint64_t pcm_c_get_core_event(uint32_t, uint32_t);
#endif


int main(int argc, const char *argv[])
{
	int i,a[100],b[100],c[100];
	int lcore_id;

#ifdef PCM_DYNAMIC_LIB
	void * handle = dlopen("libpcm.so", RTLD_LAZY);
	if(!handle) {
		printf("Abort: could not (dynamically) load shared library \n");
		return -1;
	}

	PCM.pcm_c_build_core_event = (int (*)(uint8_t, const char *)) dlsym(handle, "pcm_c_build_core_event");
	PCM.pcm_c_init = (int (*)()) dlsym(handle, "pcm_c_init");
	PCM.pcm_c_start = (void (*)()) dlsym(handle, "pcm_c_start");
	PCM.pcm_c_stop = (void (*)()) dlsym(handle, "pcm_c_stop");
	PCM.pcm_c_get_cycles = (uint64_t (*)(uint32_t)) dlsym(handle, "pcm_c_get_cycles");
	PCM.pcm_c_get_instr = (uint64_t (*)(uint32_t)) dlsym(handle, "pcm_c_get_instr");
	PCM.pcm_c_get_core_event = (uint64_t (*)(uint32_t,uint32_t)) dlsym(handle, "pcm_c_get_core_event");
#else
	PCM.pcm_c_build_core_event = pcm_c_build_core_event;
	PCM.pcm_c_init = pcm_c_init;
	PCM.pcm_c_start = pcm_c_start;
	PCM.pcm_c_stop = pcm_c_stop;
	PCM.pcm_c_get_cycles = pcm_c_get_cycles;
	PCM.pcm_c_get_instr = pcm_c_get_instr;
	PCM.pcm_c_get_core_event = pcm_c_get_core_event;
#endif

	if(PCM.pcm_c_init == NULL || PCM.pcm_c_start == NULL || PCM.pcm_c_stop == NULL ||
			PCM.pcm_c_get_cycles == NULL || PCM.pcm_c_get_instr == NULL ||
			PCM.pcm_c_build_core_event == NULL)
		return -1;
	switch(argc-1)
	{
		case 4:
			PCM.pcm_c_build_core_event(3,argv[3]);
		case 3:	
			PCM.pcm_c_build_core_event(2,argv[2]);
		case 2:
			PCM.pcm_c_build_core_event(1,argv[2]);
		case 1:
			PCM.pcm_c_build_core_event(0,argv[1]);
		case 0:
			break;
		default:
			printf("Number of arguments are too many! exit...\n");
			return -2;
	}

	PCM.pcm_c_init();
	PCM.pcm_c_start();
	for(i=0;i<10000;i++)
            c[i%100] = 4 * a[i%100] + b[i%100];
	PCM.pcm_c_stop();

	lcore_id = pcm_getcpu();
	printf("C:%lu I:%lu, IPC:%3.2f\n",
		PCM.pcm_c_get_cycles(lcore_id),
		PCM.pcm_c_get_instr(lcore_id),
		(double)PCM.pcm_c_get_instr(lcore_id)/PCM.pcm_c_get_cycles(lcore_id));
	printf("CPU%d E0: %lu, E1: %lu, E2: %lu, E3: %lu\n",
		lcore_id,
		PCM.pcm_c_get_core_event(lcore_id,0),
		PCM.pcm_c_get_core_event(lcore_id,1),
		PCM.pcm_c_get_core_event(lcore_id,2),
		PCM.pcm_c_get_core_event(lcore_id,3));

	return 0;
}
