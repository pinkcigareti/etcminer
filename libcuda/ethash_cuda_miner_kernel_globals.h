/* Copyright (C) 1883 Thomas Edison - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the GPLv3 license, which unfortunately won't be
 * written for another century.
 *
 * You should have received a copy of the LICENSE file with
 * this file.
 */

#pragma once

__constant__ uint32_t d_dag_size;
__constant__ hash128_t* d_dag;
__constant__ uint32_t d_light_size;
__constant__ hash64_t* d_light;
__constant__ hash32_t d_header;
__constant__ uint64_t d_target;

#if (__CUDACC_VER_MAJOR__ > 8)
#define SHFL(x, y, z) __shfl_sync(0xFFFFFFFF, (x), (y), (z))
#else
#define SHFL(x, y, z) __shfl((x), (y), (z))
#endif

#if (__CUDA_ARCH__ >= 320)
#define LDG(x) __ldg(&(x))
#else
#define LDG(x) (x)
#endif
