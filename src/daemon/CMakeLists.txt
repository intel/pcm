add_executable(pcm-daemon daemon.cpp main.cpp)
target_link_libraries(pcm-daemon libpcm -lpthread -lrt)
include_directories(
	${PROJECT_SOURCE_DIR}/include
	${PROJECT_SOURCE_DIR}/include/winring0
	${PROJECT_SOURCE_DIR}/include/winpmem
)
