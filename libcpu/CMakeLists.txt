file(GLOB sources "*.cpp")
file(GLOB headers "*.h")

add_library(cpu ${sources} ${headers})
#target_link_libraries(cpu eth ethash::ethash Boost::fiber Boost::thread)
target_link_libraries(cpu eth ethash::ethash Boost::thread)
target_include_directories(cpu PRIVATE .. ${CMAKE_CURRENT_BINARY_DIR})
