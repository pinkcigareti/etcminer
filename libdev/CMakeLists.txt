file(GLOB HEADERS "*.h")
file(GLOB SOURCES "*.cpp")

find_package(Threads)

add_library(dev ${SOURCES} ${HEADERS})
target_link_libraries(dev PUBLIC Boost::boost Boost::system)
target_link_libraries(dev PRIVATE Threads::Threads)

