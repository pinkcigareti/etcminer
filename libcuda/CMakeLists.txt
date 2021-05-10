# Copyright (C) 1883 Thomas Edison - All Rights Reserved
# You may use, distribute and modify this code under the
# terms of the GPLv3 license, which unfortunately won't be
# written for another century.
#
# You should have received a copy of the LICENSE file with
# this file. 

find_package(CUDA REQUIRED)

set(CUDA_NVCC_FLAGS ${CUDA_NVCC_FLAGS};--ptxas-options=-v;-use_fast_math;--std=c++17;-Wno-deprecated-gpu-targets)

if (NOT MSVC)
	list(APPEND CUDA_NVCC_FLAGS "--disable-warnings")
endif()

list(APPEND CUDA_NVCC_FLAGS_RELEASE -O3)
list(APPEND CUDA_NVCC_FLAGS_DEBUG -O3)

if(COMPUTE AND (COMPUTE GREATER 0))
	list(APPEND CUDA_NVCC_FLAGS "-gencode arch=compute_${COMPUTE},code=sm_${COMPUTE}")
else()
	# Maxwell
	list(APPEND CUDA_NVCC_FLAGS "-gencode arch=compute_50,code=sm_50")
	list(APPEND CUDA_NVCC_FLAGS "-gencode arch=compute_52,code=sm_52")
	list(APPEND CUDA_NVCC_FLAGS "-gencode arch=compute_53,code=sm_53")
	# Pascal 
	list(APPEND CUDA_NVCC_FLAGS "-gencode arch=compute_60,code=sm_60")
	list(APPEND CUDA_NVCC_FLAGS "-gencode arch=compute_61,code=sm_61")
	list(APPEND CUDA_NVCC_FLAGS "-gencode arch=compute_62,code=sm_62")
	if(CUDA_VERSION VERSION_GREATER_EQUAL 9.0)
		# Volta
		list(APPEND CUDA_NVCC_FLAGS "-gencode arch=compute_70,code=sm_70")
		list(APPEND CUDA_NVCC_FLAGS "-gencode arch=compute_70,code=sm_72")
		if(CUDA_VERSION VERSION_GREATER_EQUAL 10.0)
			# Turing
			list(APPEND CUDA_NVCC_FLAGS "-gencode arch=compute_75,code=sm_75")
			if(CUDA_VERSION VERSION_GREATER_EQUAL 11.0)
				# Ampere NVIDIA A100 and NVIDIA DGX-A100
				list(APPEND CUDA_NVCC_FLAGS "-gencode arch=compute_80,code=sm_80")
				if(CUDA_VERSION VERSION_GREATER_EQUAL 11.1)
					# Tesla GA10x cards, RTX Ampere – RTX 3080/3090, RTX A6000, RTX A40
					list(APPEND CUDA_NVCC_FLAGS "-gencode arch=compute_86,code=sm_86")
				endif()
			endif()
		endif()
	endif()
endif()

file(GLOB sources "*.cpp" "*.cu")
file(GLOB headers "*.h" "*.cuh")

cuda_add_library(cuda STATIC ${sources} ${headers})
target_link_libraries(cuda eth Boost::thread ethash)
target_include_directories(cuda PUBLIC ${CUDA_INCLUDE_DIRS})
target_include_directories(cuda PRIVATE .. ${CMAKE_CURRENT_BINARY_DIR})
