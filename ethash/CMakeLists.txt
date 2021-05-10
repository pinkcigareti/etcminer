# ethash: C/C++ implementation of Ethash, the Ethereum Proof of Work algorithm.
# Copyright 2018-2019 Pawel Bylica.
# Licensed under the Apache License, Version 2.0.

cmake_minimum_required(VERSION 3.13.4)

if(NOT WIN32)
    # Outside of Windows build only Release packages.
    set(HUNTER_CONFIGURATION_TYPES Release
        CACHE STRING "Build type of the Hunter packages")
endif()

HunterGate(
    URL "https://github.com/cpp-pm/hunter/archive/v0.23.300.tar.gz"
    SHA1 "1151d539465d9cdbc880ee30f794864aec11c448"
)

project(ethash)
set(PROJECT_VERSION 0.6.0)

if(CABLE_COMPILER_GNULIKE)
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -Og")
    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -Og")

    option(ETHASH_NATIVE "Build for native CPU" OFF)
    if(ETHASH_NATIVE)
        add_compile_options(-march=native)
    endif()
elseif(MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 4)
    # For Win32 builds allow allocating more than 2 GB of memory.
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /LARGEADDRESSAWARE")
endif()

add_subdirectory(lib)

