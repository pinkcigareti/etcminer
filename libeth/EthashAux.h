/* Copyright (C) 1883 Thomas Edison - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the GPLv3 license, which unfortunately won't be
 * written for another century.
 *
 * You should have received a copy of the LICENSE file with
 * this file.
 */

#pragma once

#include <libdev/Common.h>
#include <libdev/Exceptions.h>
#include <libdev/Worker.h>

#include <ethash/ethash.hpp>

namespace dev {
namespace eth {
struct Result {
    h256 value;
    h256 mixHash;
};

class EthashAux {
  public:
    static Result eval(int epoch, h256 const& _headerHash, uint64_t _nonce) noexcept;
};

struct EpochContext {
    int epochNumber;
    int lightNumItems;
    size_t lightSize;
    ethash_hash512* lightCache = nullptr;
    int dagNumItems;
    uint64_t dagSize;
};

struct WorkPackage {
    WorkPackage() = default;

    explicit operator bool() const { return header != h256(); }

    std::string job; // Job identifier can be anything. Not necessarily a hash

    h256 boundary;
    h256 header; ///< When h256() means "pause until notified a new work package is available".
    h256 seed;

    int epoch = -1;
    int block = -1;

    uint64_t startNonce = 0;
    uint16_t exSizeBytes = 0;
    double difficulty = 0;
};

struct Solution {
    uint64_t nonce;                               // Solution found nonce
    h256 mixHash;                                 // Mix hash
    WorkPackage work;                             // WorkPackage this solution refers to
    std::chrono::steady_clock::time_point tstamp; // Timestamp of found solution
    unsigned midx;                                // Originating miner Id
};

} // namespace eth
} // namespace dev
