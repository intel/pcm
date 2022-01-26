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

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/examples)

# Linux examples
if(UNIX)
    # Example part
    file(GLOB EXAMPLE_FILE c_example.c)

    # create static lib example
    add_executable(c_example ${EXAMPLE_FILE})
    add_dependencies(c_example PCM_SHARED)
    set_target_properties(c_example PROPERTIES
      BUILD_WITH_INSTALL_RPATH TRUE
      INSTALL_RPATH_USE_LINK_PATH TRUE
      INSTALL_RPATH "${CMAKE_BINARY_DIR}/lib:${INSTALL_RPATH}")   # rpath to libpcm.so
    target_compile_definitions(c_example PRIVATE PCM_DYNAMIC_LIB) # -DPCM_DYNAMIC_LIB
    target_link_libraries(c_example PRIVATE Threads::Threads dl)  # -pthread -ldl

    # create shared lib example
    add_executable(c_example_shlib ${EXAMPLE_FILE})
    target_link_libraries(c_example_shlib PUBLIC PCM_SHARED PRIVATE Threads::Threads)
endif(UNIX)