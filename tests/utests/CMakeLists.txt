# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2009-2025, Intel Corporation

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/tests/utests)

project(pcm_utests LANGUAGES CXX)

enable_testing()

include_directories(${CMAKE_SOURCE_DIR}/src/)
include_directories("${GMOCK_DIR}/include")

file(GLOB LSPCI_TEST_FILES lspci-utest.cpp ${CMAKE_SOURCE_DIR}/src/lspci.cpp)
set(LIBS PCM_STATIC)
add_executable(lspci-utest ${LSPCI_TEST_FILES})

target_link_libraries(
    lspci-utest
    GTest::gtest_main
    GTest::gmock_main
    ${LIBS}
)

include(GoogleTest)
gtest_discover_tests(lspci-utest)
