# Copyright (c) 2014 Intel Corporation

all: libintelpcm.so

OPT= -g -O3
CXXFLAGS+= -Wall -fPIC $(OPT) -std=c++0x
vpath %.cpp ..

libintelpcm.so: msr.o cpucounters.o pci.o client_bw.o utils.o
	$(CXX) $(CXXFLAGS) -shared $^ -lpthread -o $@

clean:
	rm -rf *.x *.o *~ *.so


