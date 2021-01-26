
#pragma once

#include "ethash_cuda_miner_kernel.h"

#include <libdevcore/Worker.h>
#include <libethcore/EthashAux.h>
#include <libethcore/Miner.h>

#include <functional>

#define MAX_STREAMS 4
#define CU_TARGET_BATCH_TIME 0.9F  // seconds

namespace dev
{
namespace eth
{
class CUDAMiner : public Miner
{
public:
    CUDAMiner(unsigned _index, DeviceDescriptor& _device);
    ~CUDAMiner() override;

    static int getNumDevices();
    static void enumDevices(std::map<string, DeviceDescriptor>& _DevicesCollection);

    void search(
        uint8_t const* header, uint64_t target, uint64_t _startN, const dev::eth::WorkPackage& w);

protected:
    bool initDevice() override;

    void initEpoch() override;

    void kick_miner() override;

private:
    void workLoop() override;

    Search_results* m_search_buf[MAX_STREAMS];
    cudaStream_t m_streams[MAX_STREAMS];
    uint64_t m_current_target = 0;

    uint64_t m_allocated_memory_dag = 0; // dag_size is a uint64_t in EpochContext struct
    size_t m_allocated_memory_light_cache = 0;

    volatile bool m_done = true;
};


}  // namespace eth
}  // namespace dev
