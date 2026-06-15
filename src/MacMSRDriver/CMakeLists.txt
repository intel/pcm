# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2022, Intel Corporation

set(CMAKE_MACOSX_RPATH 1)

set(CMAKE_CXX_FLAGS "-Wall")
set(CMAKE_CXX_FLAGS_RELEASE "-O3")
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g")

file(GLOB LIB_FILES PCIDriverInterface.cpp MSRAccessor.cpp)

find_library(IOKIT_LIBRARY IOKit)
add_library(PcmMsr SHARED ${LIB_FILES})
target_link_libraries(PcmMsr PRIVATE ${IOKIT_LIBRARY})

add_subdirectory(PcmMsr)

# Installation
install(TARGETS PcmMsr DESTINATION "lib")
