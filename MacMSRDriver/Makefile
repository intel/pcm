#
# Copyright (c) 2012 Intel Corporation
# written by Austen Ott 
#
# Build and install the PcmMsr kext and dynamically linked library.

clean:
	xcodebuild clean

kext:
	xcodebuild -configuration Release -target PcmMsrDriver build

library:
	xcodebuild -configuration Release -target PcmMsrLibrary build

install: clean kext library
	sudo sh ./kextload.sh
	sudo mkdir -p /usr/local/include /usr/local/lib
	sudo cp build/Release/libPcmMsr.dylib /usr/local/lib/
	sudo cp MSRAccessorPublic.h /usr/local/include/MSRAccessor.h
	sudo cp MSRKernel.h /usr/local/include/MSRKernel.h
	sudo cp PCIDriverInterface.h /usr/local/include/PCIDriverInterface.h

uninstall:
	sudo sh ./kextunload.sh
	sudo rm /usr/local/include/MSRKernel.h
	sudo rm /usr/local/include/MSRAccessor.h
	sudo rm /usr/local/lib/libPcmMsr.dylib
	sudo rm /usr/local/include/PCIDriverInterface.h
