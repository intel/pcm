# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2022-2024, Intel Corporation

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

if(PCM_FUZZ)
    find_package(OpenSSL REQUIRED)
    set(SSL_LIBS OpenSSL::SSL OpenSSL::Crypto)
    add_executable(urltest-fuzz urltest-fuzz.cpp)
    add_executable(pcm-sensor-server-fuzz pcm-sensor-server-fuzz.cpp)
    add_executable(pcm-sensor-server-ssl-fuzz pcm-sensor-server-fuzz.cpp)
    target_compile_options(pcm-sensor-server-fuzz PRIVATE -DUSE_SSL=1)
    target_compile_options(pcm-sensor-server-ssl-fuzz PRIVATE -DUSE_SSL=1 -DFUZZ_USE_SSL=1)
    add_executable(pcm-fuzz pcm-fuzz.cpp)
    add_executable(pcm-memory-fuzz pcm-memory-fuzz.cpp)
    target_link_libraries(urltest-fuzz Threads::Threads PCM_STATIC)
    target_link_libraries(pcm-sensor-server-fuzz Threads::Threads PCM_STATIC ${SSL_LIBS})
    target_link_libraries(pcm-sensor-server-ssl-fuzz Threads::Threads PCM_STATIC ${SSL_LIBS})
    target_link_libraries(pcm-fuzz Threads::Threads PCM_STATIC)
    target_link_libraries(pcm-memory-fuzz Threads::Threads PCM_STATIC)
endif()
