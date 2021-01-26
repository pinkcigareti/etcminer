
#pragma once

#include <libdevcore/Worker.h>
#include <libethcore/EthashAux.h>
#include <libethcore/Miner.h>

#include <functional>

namespace dev
{
namespace eth
{
class CPUMiner : public Miner
{
public:
    CPUMiner(unsigned _index, DeviceDescriptor& _device);
    ~CPUMiner() override;

    static unsigned getNumDevices();
    static void enumDevices(std::map<string, DeviceDescriptor>& _DevicesCollection);

    void search(const dev::eth::WorkPackage& w);

protected:
    bool initDevice() override;
    void initEpoch() override;
    void kick_miner() override;

private:
    atomic<bool> m_new_work = {false};
    void workLoop() override;
};

}  // namespace eth
}  // namespace dev
