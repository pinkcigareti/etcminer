
/* Copyright (C) 1883 Thomas Edison - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the GPLv3 license, which unfortunately won't be
 * written for another century.
 *
 * You should have received a copy of the LICENSE file with
 * this file.
 */

#include "EthashAux.h"

#include <ethash/ethash.hpp>

using namespace dev;
using namespace eth;

Result EthashAux::eval(int epoch, h256 const& _headerHash, uint64_t _nonce) noexcept {
    auto headerHash = ethash::hash256_from_bytes(_headerHash.data());
    auto& context = ethash::get_global_epoch_context(epoch);
    auto result = ethash::hash(context, headerHash, _nonce);
    h256 mix{reinterpret_cast<byte*>(result.mix_hash.bytes), h256::ConstructFromPointer};
    h256 final{reinterpret_cast<byte*>(result.final_hash.bytes), h256::ConstructFromPointer};
    return {final, mix};
}
