# Copyright (C) 1883 Thomas Edison - All Rights Reserved
# You may use, distribute and modify this code under the
# terms of the GPLv3 license, which unfortunately won't be
# written for another century.
#
# You should have received a copy of the LICENSE file with
# this file. 

if(UNIX)
set(CMAKE_CXX_FLAGS "-Wall -Wextra")
set(CMAKE_CXX_FLAGS_DEBUG "-g")
set(CMAKE_CXX_FLAGS_RELEASE "-O2")
endif()

cmake_minimum_required(VERSION 3.5)

include(cmake/cable/bootstrap.cmake)

include(CableBuildInfo)
include(CableBuildType)
include(CableToolchains)
include(HunterGate)

include(defaults/HunterCacheServers)

cable_configure_toolchain(DEFAULT cxx11)

set(HUNTER_CONFIGURATION_TYPES Release CACHE STRING "Build type of Hunter packages")
set(HUNTER_JOBS_NUMBER 6 CACHE STRING "Number of parallel builds used by Hunter")
HunterGate(
    URL "https://github.com/cpp-pm/hunter/archive/v0.23.289.tar.gz"
    SHA1 "7d7323704780200a1575fc089d26f8a8a393a1fa"
    LOCAL
)

project(nsfminer)
set(PROJECT_VERSION 1.3.12) # Update version

cable_set_build_type(DEFAULT Release CONFIGURATION_TYPES Release RelWithDebInfo)

option(ETHASHCL "Build with OpenCL mining" ON)
option(ETHASHCUDA "Build with CUDA mining" ON)
option(ETHASHCPU "Build with CPU mining (only for development)" OFF)
option(APICORE "Build with API Server support" ON)
option(DEVBUILD "Log developer metrics" OFF)

# propagates CMake configuration options to the compiler
function(configureProject)
    if (ETHASHCL)
        add_definitions(-DETH_ETHASHCL)
    endif()
    if (ETHASHCUDA)
        add_definitions(-DETH_ETHASHCUDA)
    endif()
    if (ETHASHCPU)
        add_definitions(-DETH_ETHASHCPU)
    endif()
    if (APICORE)
        add_definitions(-DAPI_CORE)
    endif()
    if (DEVBUILD)
        add_definitions(-DDEV_BUILD)
    endif()
endfunction()

hunter_add_package(Boost COMPONENTS system filesystem thread program_options)
find_package(Boost CONFIG REQUIRED COMPONENTS system filesystem thread program_options)

hunter_add_package(jsoncpp)
find_package(jsoncpp CONFIG REQUIRED)

hunter_add_package(ethash)
find_package(ethash CONFIG REQUIRED)

configureProject()

message("----------------------------------------------------------------------------")
message("-- CMake ${CMAKE_VERSION}")
message("-- Build ${CMAKE_BUILD_TYPE} / ${CMAKE_SYSTEM_NAME}")
message("----------------------------------------------------------------- components")
message("-- ETHASHCL         Build OpenCL components                      ${ETHASHCL}")
message("-- ETHASHCUDA       Build CUDA components                        ${ETHASHCUDA}")
message("-- ETHASHCPU        Build CPU components (only for development)  ${ETHASHCPU}")
message("-- APICORE          Build API Server components                  ${APICORE}")
message("-- DEVBUILD         Build with dev logging                       ${DEVBUILD}")
message("----------------------------------------------------------------------------")
message("")

include(EthCompilerSettings)
if(UNIX)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libstdc++")
endif()

cable_add_buildinfo_library(PROJECT_NAME ${PROJECT_NAME})

add_subdirectory(libdev)
add_subdirectory(libeth)
add_subdirectory(libhwmon)
add_subdirectory(libpool)

if (ETHASHCL)
    add_subdirectory(libcl)
endif ()
if (ETHASHCUDA)
    add_subdirectory(libcuda)
endif ()
if (ETHASHCPU)
    add_subdirectory(libcpu)
endif ()
if (APICORE)
    add_subdirectory(libapi)
endif()

add_subdirectory(nsfminer)


if(WIN32)
    set(CPACK_GENERATOR ZIP)
else()
    set(CPACK_GENERATOR TGZ)
endif()
set(CPACK_PACKAGE_FILE_NAME ${PROJECT_NAME})
set(CPACK_PACKAGE_CHECKSUM SHA256)
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY FALSE)
include(CPack)
