
UNAME:=$(shell uname)

COMMON_FLAGS = -Wall -g -O3 -Wno-unknown-pragmas -fPIC
CFLAGS += $(COMMON_FLAGS)
CXXFLAGS += $(COMMON_FLAGS) -std=c++11

# uncomment if your Linux kernel supports access to /dev/mem from user space
# CXXFLAGS += -DPCM_USE_PCI_MM_LINUX

# rely on Linux perf support (user needs CAP_SYS_ADMIN privileges), comment out to disable
ifneq ($(wildcard /usr/include/linux/perf_event.h),)
CXXFLAGS += -DPCM_USE_PERF
endif

ifeq ($(UNAME), Linux)
CFLAGS += -pthread
LIB= -lpthread -lrt
EXE += pcm-sensor-server.x
CXXFLAGS += -Wextra -pthread
OPENSSL_LIB=
# Disabling until we can properly check for dependencies, enable
# yourself if you have the required headers and library installed
#CXXFLAGS += -DUSE_SSL
#OPENSSL_LIB=-lssl -lcrypto -lz -ldl
endif
ifeq ($(UNAME), DragonFly)
LIB= -pthread -lrt
endif
ifeq ($(UNAME), Darwin)
LIB= -lpthread MacMSRDriver/build/Release/libPcmMsr.dylib
CXXFLAGS += -I/usr/include -IMacMSRDriver
endif
ifeq ($(UNAME), FreeBSD)
CXX=c++
LIB= -lpthread -lc++
endif

COMMON_OBJS = ../msr.o ../cpucounters.o ../pci.o ../mmio.o ../bw.o ../utils.o ../topology.o ../dashboard.o ../debug.o ../threadpool.o ../resctrl.o ../simdjson_wrapper.o

urltest: urltest.cpp ../pcm-sensor-server.cpp
	$(CXX) $(CXFLAGS) urltest.cpp $(COMMON_OBJS) $(LIB) -o urltest
