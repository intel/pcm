
add_subdirectory(daemon)
add_subdirectory(client)

if(WIN32)
	set(win_SRCS
		stdafx.cpp
	)
endif()

set(common_SRCS
	${win_SRCS}
	client_bw.cpp
	cpucounters.cpp
	mmio.cpp
	msr.cpp
	pci.cpp
	utils.cpp
)
include_directories(
	${PROJECT_SOURCE_DIR}/include
	${PROJECT_SOURCE_DIR}/include/winring0
	${PROJECT_SOURCE_DIR}/include/winpmem
)

add_library(libpcm ${common_SRCS})
install(TARGETS libpcm
	DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

set(program_NAMES
	pcm
	pcm-core
	pcm-iio
	pcm-latency
	pcm-lspci
	pcm-memory
	pcm-msr
	pcm-numa
	pcm-pcicfg
	pcm-pcie
	pcm-power
	pcm-sensor
	pcm-tsx
	memoptest
	msrtest
	readmem
	realtime
)

foreach(item ${program_NAMES})
	MESSAGE("Adding target: ${item}")
	add_executable(${item} ${item}.cpp)
	target_link_libraries(${item} libpcm -lpthread -lrt)
	install(
		TARGETS ${item}
		DESTINATION ${CMAKE_INSTALL_BINDIR}
	)
endforeach()

