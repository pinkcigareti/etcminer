# ethash: C/C++ implementation of Ethash, the Ethereum Proof of Work algorithm.
# Copyright 2019 Pawel Bylica.
# SPDX-License-Identifier: Apache-2.0

add_library(keccak)
add_library(ethash::keccak ALIAS keccak)
target_compile_features(keccak PUBLIC c_std_99)
target_include_directories(keccak PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_sources(keccak PRIVATE
    ${PROJECT_SOURCE_DIR}/include/ethash/keccak.h
    ${PROJECT_SOURCE_DIR}/include/ethash/keccak.hpp
    keccak.c
    keccakf800.c
)
