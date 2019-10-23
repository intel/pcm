add_executable(pcm-client client.cpp main.cpp)
target_link_libraries(pcm-client libpcm -lpthread -lrt)
include_directories(
	${PROJECT_SOURCE_DIR}/include
	${PROJECT_SOURCE_DIR}/include/winring0
	${PROJECT_SOURCE_DIR}/include/winpmem
)
