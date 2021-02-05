set(SOURCES
    ApiServer.h ApiServer.cpp
)

add_library(api ${SOURCES})
target_link_libraries(api PRIVATE eth dev nsfminer-buildinfo Boost::filesystem)
target_include_directories(api PRIVATE ..)
