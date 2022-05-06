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


# All pcm-* executables
set(PROJECT_NAMES pcm pcm-numa pcm-latency pcm-power pcm-msr pcm-memory pcm-tsx pcm-pcie pcm-core pcm-iio pcm-lspci pcm-pcicfg pcm-mmio pcm-raw)

file(GLOB COMMON_SOURCES msr.cpp cpucounters.cpp pci.cpp mmio.cpp bw.cpp utils.cpp topology.cpp debug.cpp threadpool.cpp)
file(GLOB UNUX_SOURCES dashboard.cpp resctrl.cpp)

if(UNIX)  # LINUX, FREE_BSD, APPLE
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS} -s")  # --strip-unneeded for packaging

    list(APPEND PROJECT_NAMES pcm-sensor)
    if(LINUX)
        list(APPEND PROJECT_NAMES pcm-sensor-server)
    endif()

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

    # libpcm.so
    add_library(PCM_SHARED SHARED pcm-core.cpp)
    # PCM_SILENT in Release* for pcm-core.cpp
    target_compile_definitions(PCM_SHARED PRIVATE
      $<$<CONFIG:Release>:PCM_SILENT>
      $<$<CONFIG:MinSizeRel>:PCM_SILENT>
      $<$<CONFIG:RelWithDebInfo>:PCM_SILENT>
    )
    if(APPLE)
        add_subdirectory(MacMSRDriver)
        include_directories("${CMAKE_SOURCE_DIR}/src/MacMSRDriver") # target_include_directories doesn't work
        target_link_libraries(PCM_SHARED PRIVATE PCM_STATIC_SILENT PcmMsr Threads::Threads)
    else()
        target_link_libraries(PCM_SHARED PRIVATE PCM_STATIC_SILENT Threads::Threads)
    endif()
    set_target_properties(PCM_SHARED PROPERTIES OUTPUT_NAME pcm)
endif()

if(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /O2 /wd4251 /wd4273 /EHa /Zi")
    add_definitions(/W3)

    # https://cmake.org/cmake/help/latest/prop_tgt/MSVC_RUNTIME_LIBRARY.html
    # windows/restrictDriverAccess.cpp is building separately
    add_library(restrictDriverAccess OBJECT windows/restrictDriverAccess.cpp)
    set_target_properties(restrictDriverAccess PROPERTIES COMMON_LANGUAGE_RUNTIME "")
    target_compile_definitions(restrictDriverAccess PRIVATE _CONSOLE _UNICODE UNICODE)
        
    # Rest of windows/* files + restrictDriverAccess.cpp -> PCM_STATIC
    file(GLOB WINDOWS_SOURCES winpmem/winpmem.cpp windows/stdafx.cpp freegetopt/getopt.cpp)
    add_library(PCM_STATIC STATIC $<TARGET_OBJECTS:restrictDriverAccess> ${COMMON_SOURCES} ${WINDOWS_SOURCES})
    target_compile_definitions(PCM_STATIC PRIVATE UNICODE _UNICODE _CONSOLE)

    # Graphical perfmon front-end: pcm-lib, pcm-service
    # Files: COMMON_FILES() + pcm-lib.cpp winpmem\winpmem.cpp dllmain.cpp
    file(GLOB PCM_LIB_SOURCES winpmem/winpmem.cpp dllmain.cpp pcm-lib.cpp )
    add_library(pcm-lib SHARED $<TARGET_OBJECTS:restrictDriverAccess> ${COMMON_SOURCES} ${PCM_LIB_SOURCES})
    target_compile_definitions(pcm-lib PRIVATE _WINDOWS _USRDLL PCM_EXPORTS _WINDLL _UNICODE UNICODE)

    # Pcm-service files: PCM_SHARED + AssemblyInfo.cpp PCMInstaller.cpp PCMService.cpp
    file(GLOB PCM_SERVICE_SOURCES windows/PCMInstaller.cpp  windows/PCMService.cpp windows/AssemblyInfo.cpp winddows/utils.cpp)
    add_executable(pcm-service $<TARGET_OBJECTS:restrictDriverAccess> ${PCM_SERVICE_SOURCES})
    target_compile_definitions(pcm-service PRIVATE _UNICODE UNICODE _CONSOLE)
    set_target_properties(pcm-service PROPERTIES LINK_FLAGS "/INCREMENTAL:NO" COMMON_LANGUAGE_RUNTIME "")
    set_property(TARGET pcm-service PROPERTY VS_DOTNET_REFERENCES "System;System.Configuration.Install;System.Data;System.Management;System.ServiceProcess;System.Xml")
    target_link_libraries(pcm-service pcm-lib)

endif(MSVC)


foreach(PROJECT_NAME ${PROJECT_NAMES})
    file(GLOB PROJECT_FILE ${PROJECT_NAME}.cpp)
    set(LIBS PCM_STATIC)

    add_executable(${PROJECT_NAME} ${PROJECT_FILE})

    # specific file for pcm-raw project
    if(${PROJECT_NAME} STREQUAL pcm-raw)
        set(LIBS ${LIBS} PCM_SIMDJSON)
    endif(${PROJECT_NAME} STREQUAL pcm-raw)

    if(LINUX OR FREE_BSD)
        set(LIBS ${LIBS} Threads::Threads)
        install(TARGETS ${PROJECT_NAME} DESTINATION "sbin")
    endif(LINUX OR FREE_BSD)

    if(APPLE)
        set(LIBS ${LIBS} Threads::Threads PcmMsr)
        install(TARGETS ${PROJECT_NAME} DESTINATION "sbin")
    endif(APPLE)

    if(MSVC)
        target_compile_definitions(${PROJECT_NAME} PRIVATE _UNICODE UNICODE _CONSOLE) # for all, except pcm-lib and pcm-service
    endif(MSVC)

    target_link_libraries(${PROJECT_NAME} PRIVATE ${LIBS})
endforeach(PROJECT_NAME ${PROJECT_NAMES})

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
        install(TARGETS daemon DESTINATION "sbin")

        file(GLOB CLIENT_SOURCES "client/*.cpp")
        add_executable(client ${CLIENT_SOURCES})
        target_link_libraries(client PRIVATE Threads::Threads)
        set_target_properties(client PROPERTIES OUTPUT_NAME "pcm-client")
        install(TARGETS client DESTINATION "bin")
    endif(LINUX)

    # Install extra files
    install(FILES pcm-bw-histogram.sh
            DESTINATION "sbin"
            RENAME pcm-bw-histogram
            PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ GROUP_EXECUTE GROUP_READ WORLD_EXECUTE WORLD_READ)
    file(GLOB OPCODE_FILES "opCode*.txt")
    foreach(opcode_file ${OPCODE_FILES})
        get_filename_component(opcode_file_name ${opcode_file} NAME)
        configure_file(${opcode_file} ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${opcode_file_name} COPYONLY)
        install(FILES ${opcode_file} DESTINATION "share/pcm")
    endforeach(opcode_file ${OPCODE_FILES})

    file(COPY "PMURegisterDeclarations" DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
    install(DIRECTORY "PMURegisterDeclarations" DESTINATION "share/pcm")

    # Install docs
    install(FILES ${CMAKE_SOURCE_DIR}/LICENSE DESTINATION "share/licenses/pcm/")
    install(FILES ${CMAKE_SOURCE_DIR}/README.md DESTINATION "share/doc/pcm/")

    file(GLOB DOC_FILES  ${CMAKE_SOURCE_DIR}/doc/*.txt  ${CMAKE_SOURCE_DIR}/doc/*.md)
    foreach(doc_file ${DOC_FILES})
        get_filename_component(doc_file_name ${doc_file} NAME)
        configure_file(${doc_file} ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${doc_file_name} COPYONLY)
        install(FILES ${doc_file} DESTINATION "share/doc/pcm/")
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
endif(MSVC)
