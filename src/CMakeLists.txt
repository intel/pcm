# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2022, Intel Corporation

include(FindOpenSSL)

# All pcm-* executables
set(PROJECT_NAMES pcm pcm-numa pcm-latency pcm-power pcm-msr pcm-memory pcm-tsx pcm-pcie pcm-core pcm-iio pcm-lspci pcm-pcicfg pcm-mmio pcm-tpmi pcm-raw pcm-accel)

set(MINIMUM_OPENSSL_VERSION 1.1.1)

file(GLOB COMMON_SOURCES pcm-accel-common.cpp msr.cpp cpucounters.cpp pci.cpp mmio.cpp tpmi.cpp pmt.cpp bw.cpp utils.cpp topology.cpp debug.cpp threadpool.cpp uncore_pmu_discovery.cpp)

if (APPLE)
  file(GLOB UNUX_SOURCES dashboard.cpp)
else()
  file(GLOB UNUX_SOURCES dashboard.cpp resctrl.cpp)
endif()

if (LINUX)
    if(EXISTS "/etc/os-release") #  AND IS_READABLE "/etc/os-release" (3.29 cmake required :-( )
      file(STRINGS "/etc/os-release" OS_RELEASE_CONTENTS)
      foreach(LINE ${OS_RELEASE_CONTENTS})
          if(LINE MATCHES "^ID=")
              string(REGEX REPLACE "^ID=\"?\([a-zA-Z]+\)\"?" "\\1" OS_ID ${LINE})
          endif()
      endforeach()
      message(STATUS "Detected Linux distribution: ${OS_ID}")
    else()
      message(STATUS "Unable to read /etc/os-release")
    endif()
endif()

if(NOT PCM_NO_ASAN)
    if(OS_ID STREQUAL "centos")
        set(PCM_NO_STATIC_LIBASAN ON)
        message(STATUS "CentOS detected, using dynamic libasan")
    endif()
    if(OS_ID STREQUAL "arch")
        set(PCM_NO_STATIC_LIBASAN ON)
        message(STATUS "arch Linux detected, using dynamic libasan")
    endif()
endif()

if(UNIX)  # LINUX, FREE_BSD, APPLE
    if (NOT APPLE)
      set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS} -s")  # --strip-unneeded for packaging
    endif()
    list(APPEND PROJECT_NAMES pcm-sensor-server)
    list(APPEND PROJECT_NAMES pcm-sensor)

    # libpcm.a
    add_library(PCM_STATIC STATIC ${COMMON_SOURCES} ${UNUX_SOURCES})
    set_target_properties(PCM_STATIC PROPERTIES OUTPUT_NAME pcm)

    # libpcm.a with -DPCM_SILENT for Release*
    add_library(PCM_STATIC_SILENT STATIC ${COMMON_SOURCES} ${UNUX_SOURCES})
    target_compile_definitions(PCM_STATIC_SILENT PRIVATE
      $<$<CONFIG:Release>:PCM_SILENT>
      $<$<CONFIG:MinSizeRel>:PCM_SILENT>
      $<$<CONFIG:RelWithDebInfo>:PCM_SILENT>
    )

    set_target_properties(PCM_STATIC_SILENT PROPERTIES POSITION_INDEPENDENT_CODE ON)

    # libpcm.so
    add_library(PCM_SHARED SHARED pcm-core.cpp)
    target_compile_options(PCM_SHARED PRIVATE -DPCM_SHARED_LIBRARY=1)
    # PCM_SILENT in Release* for pcm-core.cpp
    target_compile_definitions(PCM_SHARED PRIVATE
      $<$<CONFIG:Release>:PCM_SILENT>
      $<$<CONFIG:MinSizeRel>:PCM_SILENT>
      $<$<CONFIG:RelWithDebInfo>:PCM_SILENT>
    )

    if(PCM_NO_ASAN)
        set(PCM_DYNAMIC_ASAN "")
        set(PCM_STATIC_ASAN "")
    else()
        if(PCM_NO_STATIC_LIBASAN)
            message(STATUS "Using dynamic libasan")
            set(PCM_DYNAMIC_ASAN "asan")
            set(PCM_STATIC_ASAN "")
        else()
            set(PCM_DYNAMIC_ASAN "")
            set(PCM_STATIC_ASAN "-static-libasan")
            message(STATUS "Using static libasan")
            message(STATUS "To use dynamic libasan, use -DPCM_NO_STATIC_LIBASAN=1 option (required for CentOS and arch Linux)")
        endif()
    endif()

    if(APPLE)
        add_subdirectory(MacMSRDriver)
        include_directories("${CMAKE_SOURCE_DIR}/src/MacMSRDriver") # target_include_directories doesn't work
        target_link_libraries(PCM_SHARED PRIVATE PCM_STATIC_SILENT PcmMsr Threads::Threads)
    elseif(LINUX)
        target_link_libraries(PCM_SHARED PRIVATE PCM_STATIC_SILENT Threads::Threads "${PCM_DYNAMIC_ASAN}")
    else()
        target_link_libraries(PCM_SHARED PRIVATE PCM_STATIC_SILENT Threads::Threads)
    endif()
    set_target_properties(PCM_SHARED PROPERTIES OUTPUT_NAME pcm)
endif()

if(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /O2 /wd4251 /wd4273 /EHa /Zi")
    add_definitions(/W3)

    # windows/* files -> PCM_STATIC
    file(GLOB WINDOWS_SOURCES winpmem/winpmem.cpp windows/stdafx.cpp freegetopt/getopt.cpp)
    add_library(PCM_STATIC STATIC ${COMMON_SOURCES} ${WINDOWS_SOURCES})
    target_compile_definitions(PCM_STATIC PRIVATE UNICODE _UNICODE _CONSOLE)
    if(PCM_NO_STATIC_MSVC_RUNTIME_LIBRARY)
        set(PCM_MSVC_RUNTIME_LIBRARY_OPTIONS "")
    else()
        set(PCM_MSVC_RUNTIME_LIBRARY_OPTIONS "/MT$<$<CONFIG:Debug>:d>")
        message(STATUS "Using static MSVC runtime library")
        message(STATUS "To use default/dynamic MSVC runtime library, use -DNO_STATIC_MSVC_RUNTIME_LIBRARY=1 option")
    endif()
    target_compile_options(PCM_STATIC PRIVATE "${PCM_MSVC_RUNTIME_LIBRARY_OPTIONS}")

    # Graphical perfmon front-end: pcm-lib, pcm-service
    # Files: COMMON_FILES() + pcm-lib.cpp winpmem\winpmem.cpp dllmain.cpp
    file(GLOB PCM_LIB_SOURCES winpmem/winpmem.cpp dllmain.cpp pcm-lib.cpp )
    add_library(pcm-lib SHARED ${COMMON_SOURCES} ${PCM_LIB_SOURCES})
    target_compile_definitions(pcm-lib PRIVATE _WINDOWS _USRDLL PCM_EXPORTS _WINDLL _UNICODE UNICODE)
    target_compile_options(pcm-lib PRIVATE "${PCM_MSVC_RUNTIME_LIBRARY_OPTIONS}")

    # Pcm-service files: PCM_SHARED + AssemblyInfo.cpp PCMInstaller.cpp PCMService.cpp
    file(GLOB PCM_SERVICE_SOURCES windows/PCMInstaller.cpp  windows/PCMService.cpp windows/AssemblyInfo.cpp winddows/utils.cpp)
    add_executable(pcm-service ${PCM_SERVICE_SOURCES})
    target_compile_definitions(pcm-service PRIVATE _UNICODE UNICODE _CONSOLE)
    set_target_properties(pcm-service PROPERTIES LINK_FLAGS "/INCREMENTAL:NO" COMMON_LANGUAGE_RUNTIME "")
    set_property(TARGET pcm-service PROPERTY VS_DOTNET_REFERENCES "System;System.Configuration.Install;System.Data;System.Management;System.ServiceProcess;System.Xml")
    target_link_libraries(pcm-service pcm-lib)

endif(MSVC)

#######################
# SIMDJSON dependency
#######################

add_library(PCM_SIMDJSON INTERFACE) # interface library for simdjson
set(SIMDJSON_IS_APPLICABLE TRUE)    # true if simdjson can be used, default - TRUE

# check simdjson support matrix - https://github.com/simdjson/simdjson/blob/master/doc/basics.md
# > GCC 7.4, > Clang 6.0 , > MSVC 2017
if((CMAKE_CXX_COMPILER_ID STREQUAL "GNU"   AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 7.4) OR
    (CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 6) OR
    (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC"  AND  MSVC_TOOLSET_VERSION VERSION_LESS 141)) # corresponds to VS2017
     message(WARNING
           " ${CMAKE_CXX_COMPILER} ${CMAKE_CXX_COMPILER_VERSION} is incompartible with simdjson features' requirements.\n"
           " Refer to simdjson support matrix - https://github.com/simdjson/simdjson/blob/master/doc/basics.md .\n"
           " Parsing events from https://github.com/intel/perfmon won't be supported.")
        set(SIMDJSON_IS_APPLICABLE FALSE)
endif()

if(SIMDJSON_IS_APPLICABLE)
    find_package(simdjson QUIET)  # Working form Ububtu 22.04
    if(simdjson_FOUND)
        message(STATUS "System SIMDJSON is used")
        target_link_libraries(PCM_SIMDJSON INTERFACE simdjson::simdjson)
        target_compile_definitions(PCM_SIMDJSON INTERFACE SYSTEM_SIMDJSON)
        target_compile_definitions(PCM_SIMDJSON INTERFACE PCM_SIMDJSON_AVAILABLE)
    else()
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/simdjson/singleheader/simdjson.h")
            message(STATUS "Local SIMDJSON exists: ${CMAKE_CURRENT_SOURCE_DIR}/simdjson/singleheader/simdjson.h")
            file(GLOB SIMDJSON_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/simdjson/singleheader/simdjson.cpp)
            target_sources(PCM_SIMDJSON INTERFACE ${SIMDJSON_SOURCE})
            target_include_directories(PCM_SIMDJSON INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/simdjson/singleheader)
            target_compile_definitions(PCM_SIMDJSON INTERFACE PCM_SIMDJSON_AVAILABLE)
        else()
            message(WARNING
                " ${CMAKE_CURRENT_SOURCE_DIR}/simdjson/singleheader/simdjson.h doesn't exist\n"
                " Use `git clone --recursive` flag when cloning pcm repository to clone simdjson submodule as well or\n"
                " update submodule with command 'git submodule update --init --recursive' or\n"
                " run 'git clone https://github.com/simdjson/simdjson.git' in 'src' directory to get simdjson library")
        endif()
    endif(simdjson_FOUND)
endif(SIMDJSON_IS_APPLICABLE)

#######################
# End of SIMDJSON dependency section
#######################

if(PCM_BUILD_EXECUTABLES)
    foreach(PROJECT_NAME ${PROJECT_NAMES})
        file(GLOB PROJECT_FILE ${PROJECT_NAME}.cpp)
        set(LIBS PCM_STATIC)

        add_executable(${PROJECT_NAME} ${PROJECT_FILE})

        if(MSVC)
            target_compile_options(${PROJECT_NAME} PRIVATE "${PCM_MSVC_RUNTIME_LIBRARY_OPTIONS}")
        endif(MSVC)

        if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            set_target_properties(${PROJECT_NAME} PROPERTIES LINK_FLAGS "${PCM_STATIC_ASAN}")
        endif()

        # specific file for pcm-raw project
        if(${PROJECT_NAME} STREQUAL pcm-raw)
            set(LIBS ${LIBS} PCM_SIMDJSON)
        endif(${PROJECT_NAME} STREQUAL pcm-raw)

        if(${PROJECT_NAME} STREQUAL pcm-sensor-server)
          if(NO_SSL)
            message(STATUS "SSL is disabled")
          else()
            message(STATUS "Compiling with SSL support, requires libssl-dev or openssl-devel or libopenssl-devel or libopenssl-dev package installed")
            message(STATUS "To disable SSL support, use -DNO_SSL=1 option")
            find_package(OpenSSL ${MINIMUM_OPENSSL_VERSION} QUIET)
            if(OPENSSL_FOUND)
              message(STATUS "OpenSSL version ${OPENSSL_VERSION} >= ${MINIMUM_OPENSSL_VERSION}, OpenSSL support enabled")
              target_compile_options(${PROJECT_NAME} PRIVATE "-DUSE_SSL")
              set(LIBS ${LIBS} OpenSSL::SSL OpenSSL::Crypto)
            else()
              message(STATUS "OpenSSL support has been disabled, the version is less than ${MINIMUM_OPENSSL_VERSION}")
            endif()
          endif()
          file(READ pcm-sensor-server.service.in SENSOR_SERVICE_IN)
          string(REPLACE "@@CMAKE_INSTALL_SBINDIR@@" "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_SBINDIR}" SENSOR_SERVICE "${SENSOR_SERVICE_IN}")
          file(WRITE "${CMAKE_BINARY_DIR}/pcm-sensor-server.service" "${SENSOR_SERVICE}")
          file(GLOB PROJECT_FILE ${PROJECT_NAME}.cpp pcm-accel-common.h pcm-accel-common.cpp)
          target_include_directories(pcm-sensor-server PUBLIC ${CMAKE_SOURCE_DIR})
          if(LINUX_SYSTEMD)
            install(FILES "${CMAKE_BINARY_DIR}/pcm-sensor-server.service" DESTINATION "${LINUX_SYSTEMD_UNITDIR}")
          endif(LINUX_SYSTEMD)
        endif(${PROJECT_NAME} STREQUAL pcm-sensor-server)

        if(LINUX OR FREE_BSD)
            set(LIBS ${LIBS} Threads::Threads)
            install(TARGETS ${PROJECT_NAME} DESTINATION ${CMAKE_INSTALL_SBINDIR})
        endif(LINUX OR FREE_BSD)

        if(APPLE)
            set(LIBS ${LIBS} Threads::Threads PcmMsr)
            install(TARGETS ${PROJECT_NAME} DESTINATION ${CMAKE_INSTALL_SBINDIR})
        endif(APPLE)

        if(MSVC)
            target_compile_definitions(${PROJECT_NAME} PRIVATE _UNICODE UNICODE _CONSOLE) # for all, except pcm-lib and pcm-service
        endif(MSVC)

        target_link_libraries(${PROJECT_NAME} PRIVATE ${LIBS})
    endforeach(PROJECT_NAME ${PROJECT_NAMES})
endif(PCM_BUILD_EXECUTABLES)

#######################
# Install
#######################

if(UNIX) # APPLE, LINUX, FREE_BSD
    if(LINUX)
        # Daemon & client
        file(GLOB DAEMON_SOURCES "daemon/*.cpp")
        add_executable(daemon ${DAEMON_SOURCES})
        target_link_libraries(daemon PRIVATE PCM_STATIC Threads::Threads)
        set_target_properties(daemon PROPERTIES OUTPUT_NAME "pcm-daemon")
        install(TARGETS daemon DESTINATION ${CMAKE_INSTALL_SBINDIR})

        file(GLOB CLIENT_SOURCES "client/*.cpp")
        add_executable(client ${CLIENT_SOURCES})
        target_link_libraries(client PRIVATE Threads::Threads)
        set_target_properties(client PROPERTIES OUTPUT_NAME "pcm-client")
        install(TARGETS client DESTINATION ${CMAKE_INSTALL_BINDIR})
    endif(LINUX)

    # Install extra files
    install(FILES pcm-bw-histogram.sh
            DESTINATION ${CMAKE_INSTALL_SBINDIR}
            RENAME pcm-bw-histogram
            PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ GROUP_EXECUTE GROUP_READ WORLD_EXECUTE WORLD_READ)
    file(GLOB OPCODE_FILES "opCode*.txt")
    foreach(opcode_file ${OPCODE_FILES})
        get_filename_component(opcode_file_name ${opcode_file} NAME)
        configure_file(${opcode_file} ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${opcode_file_name} COPYONLY)
        install(FILES ${opcode_file} DESTINATION ${CMAKE_INSTALL_DATADIR}/pcm)
    endforeach(opcode_file ${OPCODE_FILES})

    file(COPY "PMURegisterDeclarations" DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
    install(DIRECTORY "PMURegisterDeclarations" DESTINATION ${CMAKE_INSTALL_DATADIR}/pcm)

    # Install docs
    install(FILES ${CMAKE_SOURCE_DIR}/LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/pcm)
    install(FILES ${CMAKE_SOURCE_DIR}/README.md DESTINATION ${CMAKE_INSTALL_DOCDIR})

    file(GLOB DOC_FILES  ${CMAKE_SOURCE_DIR}/doc/*.txt  ${CMAKE_SOURCE_DIR}/doc/*.md)
    foreach(doc_file ${DOC_FILES})
        get_filename_component(doc_file_name ${doc_file} NAME)
        configure_file(${doc_file} ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${doc_file_name} COPYONLY)
        install(FILES ${doc_file} DESTINATION ${CMAKE_INSTALL_DOCDIR})
    endforeach(doc_file ${DOC_FILES})

endif(UNIX)

if(MSVC)
    file(GLOB OPCODE_FILES "opCode*.txt")
    foreach(opcode_file ${OPCODE_FILES})
        add_custom_command(TARGET pcm-iio POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${opcode_file}
            $<TARGET_FILE_DIR:pcm-iio>)
    endforeach(opcode_file ${OPCODE_FILES})

    add_custom_command(TARGET pcm-raw POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:pcm-raw>/PMURegisterDeclarations")
    add_custom_command(TARGET pcm-raw POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${PROJECT_SOURCE_DIR}/src/PMURegisterDeclarations"
        "$<TARGET_FILE_DIR:pcm-raw>/PMURegisterDeclarations")
    add_custom_command(TARGET pcm-service POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${PROJECT_SOURCE_DIR}/src/windows/pcm-service.exe.config"
        $<TARGET_FILE_DIR:pcm-service>)
endif(MSVC)
