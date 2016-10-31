#
# Copyright (c) 2012 Intel Corporation
# written by Austen Ott 
#
# Build and install the PcmMsr kext and dynamically linked library.

kext:
	xcodebuild -configuration Release -target PcmMsrDriver clean build

library:
	xcodebuild -configuration Release -target PcmMsrLibrary clean build

install: kext library
	sudo sh ./kextload.sh
	sudo cp build/Release/libPcmMsr.dylib /usr/lib/
	sudo cp MSRAccessorPublic.h /usr/include/MSRAccessor.h
	sudo cp MSRKernel.h /usr/include/MSRKernel.h
	sudo cp PCIDriverInterface.h /usr/include/PCIDriverInterface.h

uninstall:
	sudo sh ./kextunload.sh
	sudo rm /usr/include/MSRKernel.h
	sudo rm /usr/include/MSRAccessor.h
	sudo rm /usr/lib/libPcmMsr.dylib
	sudo rm /usr/include/PCIDriverInterface.h
