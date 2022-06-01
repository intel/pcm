# SPDX-License-Identifier: BSD-3-Clause
# Copyright(c) 2021, Intel Corporation
#
# This is a super simplistic Makefile that allows the "user pattern"
# of 1) Download, 2) make, 3) make install
# and hides some of the cmake complexity for users
#
# NOTE: requires cmake
# NOTE2: doesn't cover all cases, just the simplest ones

.PHONY = all
all: binaries

# change this if you want to build somewhere else
# make BUILD=../pcm-release
BUILD := ./build

prepare:
	cmake -S $(PWD) -B ${BUILD}

binaries: prepare
	cmake --build ${BUILD} --parallel

clean:
	@make -C ${BUILD} clean
	
distclean:
	@rm -rf build

install: 
	make -C ${BUILD} install
