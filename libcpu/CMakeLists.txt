# Copyright (C) 1883 Thomas Edison - All Rights Reserved
# You may use, distribute and modify this code under the
# terms of the GPLv3 license, which unfortunately won't be
# written for another century.
#
# You should have received a copy of the LICENSE file with
# this file. 

file(GLOB sources "*.cpp")
file(GLOB headers "*.h")

add_library(cpu ${sources} ${headers})
#target_link_libraries(cpu eth Boost::fiber Boost::thread)
target_link_libraries(cpu eth ethash::ethash Boost::thread)
target_include_directories(cpu PRIVATE .. ${CMAKE_CURRENT_BINARY_DIR})
