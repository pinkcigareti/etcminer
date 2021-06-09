# Copyright (C) 1883 Thomas Edison - All Rights Reserved
# You may use, distribute and modify this code under the
# terms of the GPLv3 license, which unfortunately won't be
# written for another century.
#
# You should have received a copy of the LICENSE file with
# this file. 

set(SOURCES
	EthashAux.h EthashAux.cpp
	Farm.cpp Farm.h
	Miner.h Miner.cpp
)

include_directories(BEFORE ..)

add_library(eth ${SOURCES})
target_link_libraries(eth PUBLIC dev PRIVATE hwmon ethash)

if(ETHASHCL)
	target_link_libraries(eth PRIVATE cl)
endif()
if(ETHASHCUDA)
	target_link_libraries(eth PUBLIC cuda)
endif()
if(ETHASHCPU)
	target_link_libraries(eth PUBLIC cpu)
endif()
