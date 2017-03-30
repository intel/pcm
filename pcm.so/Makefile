# Copyright (c) 2014 Intel Corporation

all: libpcm.so

OPT= -g -O3
CXXFLAGS+= -Wall -fPIC $(OPT) -std=c++0x -DPCM_SILENT
vpath %.cpp ..

libpcm.so: msr.o cpucounters.o pci.o client_bw.o utils.o pcm-core.o
	$(CXX) $(CXXFLAGS) -shared $^ -lpthread -o $@

c_example.x: c_example.o
	gcc $^ -ldl -L./ -lpcm -Wl,-rpath=$(shell pwd) -o $@

clean:
	rm -rf *.x *.o *~ *.so


