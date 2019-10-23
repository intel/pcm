cmake_minimum_required(VERSION 3.1)

project(pcm)

set(VERSION_MAJOR 1)
set(VERSION_MINOR 0)
set(VERSION_PATCH 0)

set(VERSION_STRING "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}")
set(PROJECT_URL "https://github.com/opcm/pcm")

set(LIBRARY_TYPE SHARED)

## # Ensure finding 64bit libs when using 64-bit compilers
## if(CMAKE_CL_64)
##   set_property(GLOBAL PROPERTY FIND_LIBRARY_USE_LIB64_PATHS True)
## endif()

include(GNUInstallDirs)

set(LIB_INSTALL_DIR ${CMAKE_INSTALL_LIBDIR} CACHE STRING "Library directory name" FORCE)
set(INCLUDE_INSTALL_DIR ${CMAKE_INSTALL_INCLUDEDIR} CACHE STRING "Include directory name" FORCE)
set(SHARE_INSTALL_DIR ${CMAKE_INSTALL_DATAROOTDIR} CACHE STRING "Share directory name")
set(BIN_INSTALL_DIR ${CMAKE_INSTALL_BINDIR} CACHE STRING "Binary directory name")

set(EXECUTABLE_OUTPUT_PATH ${CMAKE_BINARY_DIR}/bin)
if(WIN32)
  set(LIBRARY_OUTPUT_PATH ${EXECUTABLE_OUTPUT_PATH})
  set(LIB_INSTALL_DIR lib)
  set(BIN_INSTALL_DIR bin)
else()
  set(LIBRARY_OUTPUT_PATH ${CMAKE_BINARY_DIR}/lib)
endif()

set(CMAKE_CXX_STANDARD 11)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -g -O3 -Wno-unknown-pragmas")

include(CheckIncludeFiles)
check_include_files("linux/perf_event.h" HAVE_LINUX_PERF_H)
if(HAVE_LINUX_PERF_H)
	add_definitions(-DPCM_USE_PERF)
endif()

add_subdirectory(src)
