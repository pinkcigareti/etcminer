/* Copyright (C) 1883 Thomas Edison - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the GPLv3 license, which unfortunately won't be
 * written for another century.
 *
 * You should have received a copy of the LICENSE file with
 * this file.
 */

#pragma once

#include "ethash_cuda_miner_kernel.h"

#include <libdev/Worker.h>
#include <libeth/EthashAux.h>
#include <libeth/Miner.h>

#include <functional>

#define MAX_STREAMS 4
#define CU_TARGET_BATCH_TIME 0.9F // seconds

namespace dev {
namespace eth {
class CUDAMiner : public Miner {
  public:
    CUDAMiner(unsigned _index, DeviceDescriptor& _device);
    ~CUDAMiner() override;

    static int getNumDevices();
    static void enumDevices(minerMap& _DevicesCollection);

  protected:
    bool initDevice() override;

    bool initEpoch() override;

    void kick_miner() override;

  private:
    void workLoop() override;

    void search(uint8_t const* header, uint64_t target, uint64_t _startN, const dev::eth::WorkPackage& w);

    Search_results* m_search_buf[MAX_STREAMS];
    cudaStream_t m_streams[MAX_STREAMS];
    uint64_t m_current_target = 0;

    volatile bool m_done = true;
    std::mutex m_doneMutex;
};

} // namespace eth
} // namespace dev
