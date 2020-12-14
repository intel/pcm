#
# Copyright (c) 2009-2020 Intel Corporation
# written by Roman Dementiev and Jim Harris
#

EXE = pcm.x pcm-numa.x pcm-latency.x pcm-power.x pcm-sensor.x pcm-msr.x pcm-memory.x pcm-tsx.x pcm-pcie.x pcm-core.x pcm-iio.x pcm-lspci.x pcm-pcicfg.x

EXE += pcm-mmio.x

EXE += c_example.x
EXE += c_example_shlib.x

EXE += pcm-raw.x

UNAME:=$(shell uname)

ifeq ($(UNAME), Linux)
EXE += daemon-binaries
endif

CXXFLAGS += -Wall -g -O3 -Wno-unknown-pragmas -std=c++11 -fPIC

# uncomment if your Linux kernel supports access to /dev/mem from user space
# CXXFLAGS += -DPCM_USE_PCI_MM_LINUX

# rely on Linux perf support (user needs CAP_SYS_ADMIN privileges), comment out to disable
ifneq ($(wildcard /usr/include/linux/perf_event.h),)
CXXFLAGS += -DPCM_USE_PERF
endif

ifeq ($(UNAME), Linux)
LIB= -pthread -lrt
EXE += pcm-sensor-server.x
CXXFLAGS += -Wextra
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

COMMON_OBJS = msr.o cpucounters.o pci.o mmio.o client_bw.o utils.o topology.o dashboard.o debug.o threadpool.o
EXE_OBJS = $(EXE:.x=.o)
OBJS = $(COMMON_OBJS) $(EXE_OBJS)

# ensure 'make' does not delete the intermediate .o files
.PRECIOUS: $(OBJS)

all: $(EXE) lib

lib: libPCM.a

daemon-binaries:
	make -C daemon/daemon/Debug
	make -C daemon/client/Debug

klocwork: $(EXE)

-include $(OBJS:.o=.d)
libPCM.a: $(COMMON_OBJS)
	ar -rcs $@ $^

%.x: %.o $(COMMON_OBJS)
	$(CXX) $(LDFLAGS) -o $@ $^ $(LIB)

pcm-sensor-server.o: pcm-sensor-server.cpp favicon.ico.h

pcm-sensor-server.x: pcm-sensor-server.o $(COMMON_OBJS)
	$(CXX) $(LDFLAGS) -o $@ $^ $(LIB) $(OPENSSL_LIB)

libpcm.so: $(COMMON_OBJS) pcm-core.o
	$(CXX) $(LDFLAGS) $(CXXFLAGS) -DPCM_SILENT -shared $^ $(LIB) -o $@

c_example.x: c_example.c libpcm.so
	$(CC) -DPCM_DYNAMIC_LIB $< -ldl -Wl,-rpath,$(shell pwd) -o $@

c_example_shlib.x: c_example.c libpcm.so
	$(CC) $< -L./ -Wl,-rpath,$(shell pwd) -lpcm -o $@

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $*.cpp -o $*.o
	@# the following lines generate dependency files for the
	@#  target object
	@# from http://scottmcpeak.com/autodepend/autodepend.html
	$(CXX) -MM $(CXXFLAGS) $*.cpp > $*.d
	@# these sed/fmt commands modify the .d file to add a target
	@#  rule for each .h and .cpp file with no dependencies;
	@# this will force 'make' to rebuild any objects that
	@#  depend on a file that has been renamed rather than
	@#  exiting with an error
	@mv -f $*.d $*.d.tmp
	@sed -e 's|.*:|$*.o:|' < $*.d.tmp > $*.d
	@sed -e 's/.*://' -e 's/\\$$//' < $*.d.tmp | fmt -1 | \
	  sed -e 's/^ *//' -e 's/$$/:/' >> $*.d
	@rm -f $*.d.tmp

memoptest.x: memoptest.cpp
	g++ -Wall -g -O0 -std=c++11 memoptest.cpp -o memoptest.x

dashboardtest.x: dashboardtest.cpp $(COMMON_OBJS)
	$(CXX) -o $@ $^ $(LIB)

nice:
	uncrustify --replace -c ~/uncrustify.cfg *.cpp *.h WinMSRDriver/Win7/*.h WinMSRDriver/Win7/*.c WinMSRDriver/WinXP/*.h WinMSRDriver/WinXP/*.c  PCM_Win/*.h PCM_Win/*.cpp  

prefix=/usr

ifneq ($(DESTDIR),)
	prefix=${DESTDIR}/usr
endif

install: all
	mkdir -p                                     ${prefix}/sbin/
	install -s -m 755 pcm-core.x                 ${prefix}/sbin/pcm-core
	install -s -m 755 pcm-iio.x                  ${prefix}/sbin/pcm-iio
	install -s -m 755 pcm-latency.x              ${prefix}/sbin/pcm-latency
	install -s -m 755 pcm-lspci.x                ${prefix}/sbin/pcm-lspci
	install -s -m 755 pcm-memory.x               ${prefix}/sbin/pcm-memory
	install -s -m 755 pcm-msr.x                  ${prefix}/sbin/pcm-msr
	install -s -m 755 pcm-numa.x                 ${prefix}/sbin/pcm-numa
	install -s -m 755 pcm-pcicfg.x               ${prefix}/sbin/pcm-pcicfg
	install -s -m 755 pcm-pcie.x                 ${prefix}/sbin/pcm-pcie
	install -s -m 755 pcm-power.x                ${prefix}/sbin/pcm-power
	install -s -m 755 pcm-sensor.x               ${prefix}/sbin/pcm-sensor
	install -s -m 755 pcm-tsx.x                  ${prefix}/sbin/pcm-tsx
	install -s -m 755 pcm-raw.x                  ${prefix}/sbin/pcm-raw
	install -s -m 755 pcm.x                      ${prefix}/sbin/pcm
ifeq ($(UNAME), Linux)
	mkdir -p                                     ${prefix}/bin/
	install -s -m 755 daemon/client/Debug/client ${prefix}/bin/pcm-client
	install -s -m 755 daemon/daemon/Debug/daemon ${prefix}/sbin/pcm-daemon
	install -s -m 755 pcm-sensor-server.x        ${prefix}/sbin/pcm-sensor-server
endif
	install -m 755 pcm-bw-histogram.sh           ${prefix}/sbin/pcm-bw-histogram
	mkdir -p                                     ${prefix}/share/pcm/
	install -m 644 opCode.txt                    ${prefix}/share/pcm/

clean:
	rm -rf *.x *.o *~ *.d *.a *.so
ifeq ($(UNAME), Linux)
	make -C daemon/daemon/Debug clean
	make -C daemon/client/Debug clean
endif
