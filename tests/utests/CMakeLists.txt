# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2009-2025, Intel Corporation

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_TEST_BINARY_DIR}/utests)

project(pcm_utests LANGUAGES CXX)

enable_testing()

include_directories(${CMAKE_SOURCE_DIR}/src/)
include_directories("${GMOCK_DIR}/include")

if(APPLE)
    include_directories("${CMAKE_SOURCE_DIR}/src/MacMSRDriver") # target_include_directories doesn't work
endif()

file(GLOB LSPCI_TEST_FILES lspci-utest.cpp ${CMAKE_SOURCE_DIR}/src/lspci.cpp)
file(GLOB PCM_IIO_TEST_FILES pcm-iio-utest.cpp ${CMAKE_SOURCE_DIR}/src/pcm-iio-pmu.cpp ${CMAKE_SOURCE_DIR}/src/pcm-iio-topology.cpp)
file(GLOB READ_NUMBER_TEST_FILES read-number-utest.cpp)

if(APPLE)
    set(LIBS PcmMsr Threads::Threads PCM_STATIC)
else()
    set(LIBS Threads::Threads PCM_STATIC)
endif()

add_executable(lspci-utest ${LSPCI_TEST_FILES})
add_executable(pcm-iio-utest ${PCM_IIO_TEST_FILES})
add_executable(read-number-utest ${READ_NUMBER_TEST_FILES})

configure_file(
    ${CMAKE_SOURCE_DIR}/src/opCode-6-174.txt
    ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/opCode-6-174.txt
    COPYONLY
)

target_link_libraries(
    lspci-utest
    GTest::gtest_main
    GTest::gmock_main
    ${LIBS}
)

target_link_libraries(
    pcm-iio-utest
    GTest::gtest_main
    GTest::gmock_main
    ${LIBS}
)

target_link_libraries(
    read-number-utest
    GTest::gtest_main
    GTest::gmock_main
    ${LIBS}
)

include(GoogleTest)
gtest_discover_tests(lspci-utest)
gtest_discover_tests(pcm-iio-utest)
gtest_discover_tests(read-number-utest)
