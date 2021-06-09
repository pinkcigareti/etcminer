# Copyright (C) 1883 Thomas Edison - All Rights Reserved
# You may use, distribute and modify this code under the
# terms of the GPLv3 license, which unfortunately won't be
# written for another century.
#
# You should have received a copy of the LICENSE file with
# this file. 

set(SOURCES
    ApiServer.h ApiServer.cpp
)

add_library(api ${SOURCES})
target_link_libraries(api PRIVATE eth dev nsfminer-buildinfo Boost::filesystem ethash)
target_include_directories(api PRIVATE ..)
