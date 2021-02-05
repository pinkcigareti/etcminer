# Copyright (C) 1883 Thomas Edison - All Rights Reserved
# You may use, distribute and modify this code under the
# terms of the GPLv3 license, which unfortunately won't be
# written for another century.
#
# You should have received a copy of the LICENSE file with
# this file. 

set(SOURCES
    wraphelper.cpp wraphelper.h
    wrapnvml.h wrapnvml.cpp
    wrapadl.h wrapadl.cpp
    wrapamdsysfs.h wrapamdsysfs.cpp
)

add_library(hwmon ${SOURCES})
set(HWMON_LINK_LIBRARIES dev Boost::filesystem)

if (UNIX)
	list(APPEND HWMON_LINK_LIBRARIES dl)
endif ()

target_link_libraries(hwmon ${HWMON_LINK_LIBRARIES})
target_include_directories(hwmon PRIVATE ..)

if (ETHASHCUDA)
	find_package(CUDA REQUIRED)
	target_include_directories(hwmon PUBLIC ${CUDA_INCLUDE_DIRS})
endif()
