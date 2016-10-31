#!/usr/bin/env bash

cp -R Build/Products/Release/PcmMsrDriver.kext /tmp/.
mv /tmp/PcmMsrDriver.kext /System/Library/Extensions
chown -R root:wheel /System/Library/Extensions/PcmMsrDriver.kext
kextload /System/Library/Extensions/PcmMsrDriver.kext
