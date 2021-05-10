# ethash: C/C++ implementation of Ethash, the Ethereum Proof of Work algorithm.
# Copyright 2018-2019 Pawel Bylica.
# Licensed under the Apache License, Version 2.0.

add_library(ethash)
add_library(ethash::ethash ALIAS ethash)
target_link_libraries(ethash PRIVATE ethash::keccak)
target_include_directories(ethash PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_sources(ethash PRIVATE
    bit_manipulation.h
    builtins.h
    endianness.hpp
    ${PROJECT_SOURCE_DIR}/include/ethash/ethash.h
    ${PROJECT_SOURCE_DIR}/include/ethash/ethash.hpp
    ethash-internal.hpp
    ethash.cpp
    ${PROJECT_SOURCE_DIR}/include/ethash/hash_types.h
    managed.cpp
    kiss99.hpp
    primes.h
    primes.c
)


if(CABLE_COMPILER_GNULIKE AND NOT SANITIZE MATCHES undefined)
    target_compile_options(ethash PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>)
endif()
