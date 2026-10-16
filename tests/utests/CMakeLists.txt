# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2009-2025, Intel Corporation

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_TEST_BINARY_DIR}/utests)

project(pcm_utests LANGUAGES CXX)

enable_testing()

include_directories(${CMAKE_SOURCE_DIR}/src/)
include_directories("${GMOCK_DIR}/include")

file(GLOB LSPCI_TEST_FILES lspci-utest.cpp ${CMAKE_SOURCE_DIR}/src/lspci.cpp)
file(GLOB PCM_IIO_TEST_FILES pcm-iio-utest.cpp ${CMAKE_SOURCE_DIR}/src/pcm-iio-pmu.cpp ${CMAKE_SOURCE_DIR}/src/pcm-iio-topology.cpp)
file(GLOB READ_NUMBER_TEST_FILES read-number-utest.cpp)
file(GLOB EVENT_RESOLVER_TEST_FILES event-resolver-utest.cpp ${CMAKE_SOURCE_DIR}/src/event-resolver.cpp)
file(GLOB PCM_IO_METRICS_TEST_FILES pcm-io-metrics-utest.cpp ${CMAKE_SOURCE_DIR}/src/pcm-io-metrics.cpp)
file(GLOB PCM_SENSOR_SERVER_OVERFLOW_TEST_FILES pcm-sensor-server-overflow-utest.cpp)
file(GLOB PCM_SENSOR_SERVER_CONTENT_LENGTH_TEST_FILES pcm-sensor-server-content-length-utest.cpp)
file(GLOB PCM_SENSOR_SERVER_HEADER_LIMITS_TEST_FILES pcm-sensor-server-header-limits-utest.cpp)
file(GLOB PCM_SENSOR_SERVER_HISTORY_TEST_FILES pcm-sensor-server-history-utest.cpp)

set(LIBS Threads::Threads PCM_STATIC)

add_executable(lspci-utest ${LSPCI_TEST_FILES})
add_executable(pcm-iio-utest ${PCM_IIO_TEST_FILES})
add_executable(read-number-utest ${READ_NUMBER_TEST_FILES})
add_executable(event-resolver-utest ${EVENT_RESOLVER_TEST_FILES})
add_executable(pcm-io-metrics-utest ${PCM_IO_METRICS_TEST_FILES})
add_executable(pcm-sensor-server-overflow-utest ${PCM_SENSOR_SERVER_OVERFLOW_TEST_FILES})
add_executable(pcm-sensor-server-content-length-utest ${PCM_SENSOR_SERVER_CONTENT_LENGTH_TEST_FILES})
add_executable(pcm-sensor-server-header-limits-utest ${PCM_SENSOR_SERVER_HEADER_LIMITS_TEST_FILES})
add_executable(pcm-sensor-server-history-utest ${PCM_SENSOR_SERVER_HISTORY_TEST_FILES})

configure_file(
    ${CMAKE_SOURCE_DIR}/src/opCode-6-174.txt
    ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/opCode-6-174.txt
    COPYONLY
)

# Copy perfmon data for event-resolver tests.
# build/bin/ copy is for CI (test.sh CWD), utests copy is for local dev.
# PMURegisterDeclarations is copied to build/bin/ by src/CMakeLists.txt.
file(COPY ${CMAKE_SOURCE_DIR}/perfmon
     DESTINATION ${CMAKE_BINARY_DIR}/bin)
file(COPY ${CMAKE_SOURCE_DIR}/perfmon
     DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
file(COPY ${CMAKE_SOURCE_DIR}/src/PMURegisterDeclarations
     DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})

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

target_link_libraries(
    event-resolver-utest
    GTest::gtest_main
    GTest::gmock_main
    ${LIBS}
    PCM_SIMDJSON
)

target_link_libraries(
    pcm-io-metrics-utest
    GTest::gtest_main
    GTest::gmock_main
    ${LIBS}
    PCM_SIMDJSON
)

target_link_libraries(
    pcm-sensor-server-overflow-utest
    GTest::gtest_main
    GTest::gmock_main
    ${LIBS}
)

target_link_libraries(
    pcm-sensor-server-content-length-utest
    GTest::gtest_main
    GTest::gmock_main
    ${LIBS}
)

target_link_libraries(
    pcm-sensor-server-header-limits-utest
    GTest::gtest_main
    GTest::gmock_main
    ${LIBS}
)

target_link_libraries(
    pcm-sensor-server-history-utest
    GTest::gtest_main
    GTest::gmock_main
    ${LIBS}
)

include(GoogleTest)
gtest_discover_tests(lspci-utest)
gtest_discover_tests(pcm-iio-utest)
gtest_discover_tests(read-number-utest)
gtest_discover_tests(event-resolver-utest)
gtest_discover_tests(pcm-io-metrics-utest)
gtest_discover_tests(pcm-sensor-server-overflow-utest)
gtest_discover_tests(pcm-sensor-server-content-length-utest)
gtest_discover_tests(pcm-sensor-server-header-limits-utest)
gtest_discover_tests(pcm-sensor-server-history-utest)
