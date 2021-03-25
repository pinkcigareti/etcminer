/* Copyright (C) 1883 Thomas Edison - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the GPLv3 license, which unfortunately won't be
 * written for another century.
 *
 * You should have received a copy of the LICENSE file with
 * this file.
 */

#pragma once

#include <sstream>
#include <stdexcept>
#include <stdint.h>
#include <string>

#include "cuda_runtime.h"

// It is virtually impossible to get more than
// one solution per stream hash calculation
// Leave room for up to 4 results. A power
// of 2 here will yield better CUDA optimization
#define MAX_SEARCH_RESULTS 4U
struct Search_results {
    uint32_t solCount;
    uint32_t hashCount;
    volatile uint32_t done;
    uint32_t gid[MAX_SEARCH_RESULTS];
};

#define ACCESSES 64
#define THREADS_PER_HASH (128 / 16)

typedef struct {
    uint4 uint4s[32 / sizeof(uint4)];
} hash32_t;

typedef union {
    uint32_t words[128 / sizeof(uint32_t)];
    uint2 uint2s[128 / sizeof(uint2)];
    uint4 uint4s[128 / sizeof(uint4)];
} hash128_t;

typedef union {
    uint32_t words[64 / sizeof(uint32_t)];
    uint2 uint2s[64 / sizeof(uint2)];
    uint4 uint4s[64 / sizeof(uint4)];
} hash64_t;

void set_constants(hash128_t* _dag, uint32_t _dag_size, hash64_t* _light, uint32_t _light_size);
void get_constants(hash128_t** _dag, uint32_t* _dag_size, hash64_t** _light, uint32_t* _light_size);
void set_header(hash32_t _header);
void set_target(uint64_t _target);
void run_ethash_search(uint32_t gridSize, uint32_t blockSize, cudaStream_t stream, Search_results* g_output,
                       uint64_t start_nonce);
void ethash_generate_dag(uint64_t dag_size, uint32_t blocks, uint32_t threads, cudaStream_t stream);

struct cuda_runtime_error : public virtual std::runtime_error {
    cuda_runtime_error(const std::string& msg) : std::runtime_error(msg) {}
};

#define CUDA_CALL(call)                                                                                                \
    do {                                                                                                               \
        cudaError_t err = call;                                                                                        \
        if (cudaSuccess != err) {                                                                                      \
            std::stringstream ss;                                                                                      \
            ss << "CUDA error in func " << __FUNCTION__ << " at line " << __LINE__ << ' ' << cudaGetErrorString(err);  \
            throw cuda_runtime_error(ss.str());                                                                        \
        }                                                                                                              \
    } while (0)
