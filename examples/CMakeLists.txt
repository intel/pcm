# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2022, Intel Corporation

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