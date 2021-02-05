set(SOURCES
	EthashAux.h EthashAux.cpp
	Farm.cpp Farm.h
	Miner.h Miner.cpp
)

include_directories(BEFORE ..)

add_library(eth ${SOURCES})
target_link_libraries(eth PUBLIC dev ethash::ethash PRIVATE hwmon)

if(ETHASHCL)
	target_link_libraries(eth PRIVATE cl)
endif()
if(ETHASHCUDA)
	target_link_libraries(eth PUBLIC cuda)
endif()
if(ETHASHCPU)
	target_link_libraries(eth PUBLIC cpu)
endif()
