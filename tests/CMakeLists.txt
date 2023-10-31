# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2022, Intel Corporation

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/tests)

if(UNIX)

    # daemon_alignment_test on Linux and Unix
    file(GLOB TEST_FILE daemon_alignment_test.cpp pcm-accel-common.cpp)
    add_executable(daemon_alignment_test ${TEST_FILE})
    target_link_libraries(daemon_alignment_test)

    # PCM_STATIC + pcm_sensor = urltest
    if(LINUX)
        add_executable(urltest urltest.cpp)
        target_link_libraries(urltest Threads::Threads PCM_STATIC)
    endif(LINUX)

endif(UNIX)
