# Copyright (c) 2022, Intel Corporation
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:
#
# * Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
# * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
# * Neither the name of Intel Corporation nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 
cmake_minimum_required(VERSION 3.5)

project(PCM)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

message(STATUS "System: ${CMAKE_SYSTEM}")
if(UNIX AND NOT APPLE)
    if(${CMAKE_SYSTEM_NAME} STREQUAL "FreeBSD")
        set(FREE_BSD TRUE)
    else()
        set(LINUX TRUE)
    endif()
endif()

if(UNIX)  # APPLE, LINUX, FREE_BSD
    if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
      set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type (default Release)" FORCE)
    endif()
    message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

    message(STATUS "initial CMAKE_CXX_FLAGS: ${CMAKE_CXX_FLAGS}")
    message(STATUS "initial CMAKE_C_FLAGS: ${CMAKE_C_FLAGS}")

    # required PCM common flags
    set (PCM_COMMON_FLAGS "-Wno-unknown-pragmas -fPIC")

    if(LINUX)
       set (PCM_COMMON_FLAGS "${PCM_COMMON_FLAGS} -Wextra -DPCM_USE_PERF")
    endif(LINUX)

    # adding the required PCM common flags
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${PCM_COMMON_FLAGS}")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${PCM_COMMON_FLAGS}")

    message(STATUS "CMAKE_CXX_FLAGS: ${CMAKE_CXX_FLAGS}")
    message(STATUS "CMAKE_C_FLAGS: ${CMAKE_C_FLAGS}")

    set(PCM_OPTIONAL_FLAGS "-Wall")
    set(CMAKE_CXX_FLAGS_RELEASE "${PCM_OPTIONAL_FLAGS} -O3")
    set(CMAKE_CXX_FLAGS_DEBUG "${PCM_OPTIONAL_FLAGS} -O0 -g")
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${PCM_OPTIONAL_FLAGS} -O3 -g")
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    set(CMAKE_C_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")

endif(UNIX)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)  # default is /usr/local
  set(CMAKE_INSTALL_PREFIX "/usr" CACHE PATH "..." FORCE)
else()
  set(CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}/usr" CACHE PATH "..." FORCE)
endif()

add_subdirectory(src)
add_subdirectory(examples)
add_subdirectory(tests)

message(STATUS "Install directory: ${CMAKE_INSTALL_PREFIX}")
