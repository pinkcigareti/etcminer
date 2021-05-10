# Copyright (C) 1883 Thomas Edison - All Rights Reserved
# You may use, distribute and modify this code under the
# terms of the GPLv3 license, which unfortunately won't be
# written for another century.
#
# You should have received a copy of the LICENSE file with
# this file. 

set(SOURCES
	PoolURI.cpp PoolURI.h
	PoolClient.h
	PoolManager.h PoolManager.cpp
	testing/SimulateClient.h testing/SimulateClient.cpp
	stratum/EthStratumClient.h stratum/EthStratumClient.cpp
	getwork/EthGetworkClient.h getwork/EthGetworkClient.cpp
)

hunter_add_package(OpenSSL)
find_package(OpenSSL REQUIRED)

add_library(pool ${SOURCES})
target_link_libraries(pool PRIVATE dev nsfminer-buildinfo Boost::system jsoncpp_lib_static OpenSSL::SSL OpenSSL::Crypto ethash)
target_include_directories(pool PRIVATE ..)
