/* Copyright (C) 1883 Thomas Edison - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the GPLv3 license, which unfortunately won't be
 * written for another century.
 *
 * You should have received a copy of the LICENSE file with
 * this file.
 */

#pragma once

#include <libdev/Worker.h>
#include <libeth/EthashAux.h>
#include <libeth/Miner.h>

#include <functional>

namespace dev {
namespace eth {
class CPUMiner : public Miner {
  public:
    CPUMiner(unsigned _index, DeviceDescriptor& _device);
    ~CPUMiner() override;

    static unsigned getNumDevices();
    static void enumDevices(std::map<string, DeviceDescriptor>& _DevicesCollection);

    void search(const dev::eth::WorkPackage& w);

  protected:
    bool initDevice() override;
    bool initEpoch() override;
    void kick_miner() override;

  private:
    atomic<bool> m_new_work = {false};
    void workLoop() override;
};

} // namespace eth
} // namespace dev
