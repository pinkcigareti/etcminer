# Copyright (C) 1883 Thomas Edison - All Rights Reserved
# You may use, distribute and modify this code under the
# terms of the GPLv3 license, which unfortunately won't be
# written for another century.
#
# You should have received a copy of the LICENSE file with
# this file. 

file(GLOB HEADERS "*.h")
file(GLOB SOURCES "*.cpp")

find_package(Threads)

add_library(dev ${SOURCES} ${HEADERS})
target_link_libraries(dev PUBLIC Boost::boost Boost::system)
target_link_libraries(dev PRIVATE Threads::Threads)

