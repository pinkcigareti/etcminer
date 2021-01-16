/*
This file is part of ethminer.

ethminer is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

ethminer is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with ethminer.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <libethcore/Farm.h>
#include <ethash/ethash.hpp>

#include "CUDAMiner.h"

using namespace std;
using namespace dev;
using namespace eth;

struct CUDAChannel : public LogChannel
{
    static bool name() { return false; }
    static const int verbosity = 2;
};
#define cudalog clog(CUDAChannel)

CUDAMiner::CUDAMiner(unsigned _index, DeviceDescriptor& _device)
  : Miner("cu-", _index),
    m_batch_size(_device.cuGridSize * _device.cuBlockSize),
    m_streams_batch_size(_device.cuGridSize * _device.cuBlockSize * _device.cuStreamSize),
    m_blockSize(_device.cuBlockSize),
    m_gridSize(_device.cuGridSize),
    m_streamSize(_device.cuStreamSize)
{
    m_deviceDescriptor = _device;
}

CUDAMiner::~CUDAMiner()
{
    stopWorking();
    kick_miner();
}

#define HostToDevice(dst, src, siz) CUDA_CALL(cudaMemcpy(dst, src, siz, cudaMemcpyHostToDevice))

#define DeviceToHost(dst, src, siz) CUDA_CALL(cudaMemcpy(dst, src, siz, cudaMemcpyDeviceToHost))

bool CUDAMiner::initDevice()
{
    cudalog << "Using Pci " << m_deviceDescriptor.uniqueId << ": " << m_deviceDescriptor.cuName
            << " (Compute " + m_deviceDescriptor.cuCompute + ") Memory : "
            << dev::getFormattedMemory((double)m_deviceDescriptor.totalMemory);

    // Set Hardware Monitor Info
    m_hwmoninfo.deviceType = HwMonitorInfoType::NVIDIA;
    m_hwmoninfo.devicePciId = m_deviceDescriptor.uniqueId;
    m_hwmoninfo.deviceIndex = -1;  // Will be later on mapped by nvml (see Farm() constructor)

    try
    {
        CUDA_CALL(cudaSetDevice(m_deviceDescriptor.cuDeviceIndex));
        CUDA_CALL(cudaDeviceReset());
    }
    catch (const cuda_runtime_error& ec)
    {
        cudalog << "Could not set CUDA device on Pci Id " << m_deviceDescriptor.uniqueId
                << " Error : " << ec.what();
        cudalog << "Mining aborted on this device.";
        return false;
    }
    return true;
}

void CUDAMiner::initEpoch()
{
    m_initialized = false;
    // If we get here it means epoch has changed so it's not necessary
    // to check again dag sizes. They're changed for sure
    m_current_target = 0;
    auto startInit = chrono::steady_clock::now();
    size_t RequiredTotalMemory = (m_epochContext.dagSize + m_epochContext.lightSize);
    size_t RequiredDagMemory = m_epochContext.dagSize;

    // Release the pause flag if any
    resume(MinerPauseEnum::PauseDueToInsufficientMemory);
    resume(MinerPauseEnum::PauseDueToInitEpochError);

    bool lightOnHost = false;
    try
    {
        hash128_t* dag;
        hash64_t* light;

        // If we have already enough memory allocated, we just have to
        // copy light_cache and regenerate the DAG
        if (m_allocated_memory_dag < m_epochContext.dagSize ||
            m_allocated_memory_light_cache < m_epochContext.lightSize)
        {
            // We need to reset the device and (re)create the dag
            // cudaDeviceReset() frees all previous allocated memory
            CUDA_CALL(cudaDeviceReset());
            CUDA_CALL(cudaSetDeviceFlags(cudaDeviceScheduleYield));
            CUDA_CALL(cudaDeviceSetCacheConfig(cudaFuncCachePreferL1));

            // Check whether the current device has sufficient memory every time we recreate the dag
            if (m_deviceDescriptor.totalMemory < RequiredTotalMemory)
            {
                if (m_deviceDescriptor.totalMemory < RequiredDagMemory)
                {
                    cudalog << "Epoch " << m_epochContext.epochNumber << " requires "
                            << dev::getFormattedMemory((double)RequiredDagMemory) << " memory.";
                    cudalog << "This device hasn't enough memory available. Mining suspended ...";
                    pause(MinerPauseEnum::PauseDueToInsufficientMemory);
                    return;  // This will prevent to exit the thread and
                             // Eventually resume mining when changing coin or epoch (NiceHash)
                }
                else
                    lightOnHost = true;
            }

            cudalog << "Generating DAG + Light(on " << (lightOnHost ? "host" : "GPU")
                    << ") : " << dev::getFormattedMemory((double)RequiredTotalMemory);

            // create buffer for cache
            if (lightOnHost)
            {
                CUDA_CALL(
                    cudaHostAlloc((void**)&light, m_epochContext.lightSize, cudaHostAllocDefault));
                cudalog << "WARNING: Generating DAG will take minutes, not seconds";
            }
            else
                CUDA_CALL(cudaMalloc(&light, m_epochContext.lightSize));
            m_allocated_memory_light_cache = m_epochContext.lightSize;
            CUDA_CALL(cudaMalloc((void**)&dag, m_epochContext.dagSize));
            m_allocated_memory_dag = m_epochContext.dagSize;

            // create mining buffers
            for (unsigned i = 0; i < m_streamSize; ++i)
            {
                CUDA_CALL(cudaMalloc(&m_search_buf[i], sizeof(Search_results)));
                CUDA_CALL(cudaStreamCreateWithFlags(&m_streams[i], cudaStreamNonBlocking));
            }
        }
        else
        {
            cudalog << "Generating DAG + Light (reusing buffers): "
                    << dev::getFormattedMemory((double)RequiredTotalMemory);
            get_constants(&dag, NULL, &light, NULL);
        }

        HostToDevice(light, m_epochContext.lightCache, m_epochContext.lightSize);

        set_constants(dag, m_epochContext.dagNumItems, light,
            m_epochContext.lightNumItems);  // in ethash_cuda_miner_kernel.cu

        ethash_generate_dag(m_epochContext.dagSize, m_gridSize, m_blockSize, m_streams[0]);

        cudalog << "Generated DAG + Light in "
                << chrono::duration_cast<chrono::milliseconds>(
                       chrono::steady_clock::now() - startInit)
                       .count()
                << " ms. "
                << dev::getFormattedMemory(
                       lightOnHost ? (double)(m_deviceDescriptor.totalMemory - RequiredDagMemory) :
                                     (double)(m_deviceDescriptor.totalMemory - RequiredTotalMemory))
                << " left.";
    }
    catch (const cuda_runtime_error& ec)
    {
        cudalog << "Unexpected error " << ec.what() << " on CUDA device "
                << m_deviceDescriptor.uniqueId;
        cudalog << "Mining suspended ...";
        pause(MinerPauseEnum::PauseDueToInitEpochError);
    }

    m_initialized = true;
}

void CUDAMiner::workLoop()
{
    WorkPackage current;
    current.header = h256();

    if (!initDevice())
        return;

    try
    {
        while (!shouldStop())
        {
            // Wait for work or 3 seconds (whichever the first)
            const WorkPackage w = work();
            if (!w)
            {
                unique_lock<mutex> l(miner_work_mutex);
                m_new_work_signal.wait_for(l, chrono::seconds(3));
                continue;
            }

            // Epoch change ?
            if (current.epoch != w.epoch)
            {
                initEpoch();

                // As DAG generation takes a while we need to
                // ensure we're on latest job, not on the one
                // which triggered the epoch change
                current = w;
                continue;
            }

            // Persist most recent job.
            // Job's differences should be handled at higher level
            current = w;
            uint64_t upper64OfBoundary = (uint64_t)(u64)((u256)current.boundary >> 192);

            // Eventually start searching
            search(current.header.data(), upper64OfBoundary, current.startNonce, w);
        }

        // Reset miner and stop working
        CUDA_CALL(cudaDeviceReset());
    }
    catch (cuda_runtime_error const& _e)
    {
        string _what = "GPU error: ";
        _what.append(_e.what());
        throw runtime_error(_what);
    }
}

void CUDAMiner::kick_miner()
{
    static const uint32_t one = 1;
    if (!m_done)
    {
        m_done = true;
        for (unsigned idx = 0; idx < m_streamSize; idx++)
            CUDA_CALL(cudaMemcpyAsync((uint8_t*)m_search_buf[idx] + offsetof(Search_results, done),
                &one, sizeof(one), cudaMemcpyHostToDevice));
    }
}

int CUDAMiner::getNumDevices()
{
    int deviceCount;
    cudaError_t err = cudaGetDeviceCount(&deviceCount);
    if (err == cudaSuccess)
        return deviceCount;

    if (err == cudaErrorInsufficientDriver)
    {
        int driverVersion = 0;
        cudaDriverGetVersion(&driverVersion);
        if (driverVersion == 0)
            cwarn << "No CUDA driver found";
        else
            cwarn << "Insufficient CUDA driver " << to_string(driverVersion);
    }
    else
        cwarn << "CUDA Error : " << cudaGetErrorString(err);

    return 0;
}

void CUDAMiner::enumDevices(map<string, DeviceDescriptor>& _DevicesCollection)
{
    int numDevices = getNumDevices();

    for (int i = 0; i < numDevices; i++)
    {
        string uniqueId;
        ostringstream s;
        DeviceDescriptor deviceDescriptor;
        cudaDeviceProp props;

        try
        {
            size_t freeMem, totalMem;
            CUDA_CALL(cudaGetDeviceProperties(&props, i));
            CUDA_CALL(cudaMemGetInfo(&freeMem, &totalMem));
            s << setw(2) << setfill('0') << hex << props.pciBusID << ":" << setw(2)
              << props.pciDeviceID << ".0";
            uniqueId = s.str();

            if (_DevicesCollection.find(uniqueId) != _DevicesCollection.end())
                deviceDescriptor = _DevicesCollection[uniqueId];
            else
                deviceDescriptor = DeviceDescriptor();

            deviceDescriptor.name = string(props.name);
            deviceDescriptor.cuDetected = true;
            deviceDescriptor.uniqueId = uniqueId;
            deviceDescriptor.type = DeviceTypeEnum::Gpu;
            deviceDescriptor.cuDeviceIndex = i;
            deviceDescriptor.cuDeviceOrdinal = i;
            deviceDescriptor.cuName = string(props.name);
            deviceDescriptor.totalMemory = freeMem;
            deviceDescriptor.cuCompute =
                (to_string(props.major) + "." + to_string(props.minor));
            deviceDescriptor.cuComputeMajor = props.major;
            deviceDescriptor.cuComputeMinor = props.minor;
            // deviceDescriptor.cuGridSize = int(ceil(props.multiProcessorCount * 3276.8));
            deviceDescriptor.cuGridSize = 15625;
            deviceDescriptor.cuBlockSize = 128;
            deviceDescriptor.cuStreamSize = 4;


            _DevicesCollection[uniqueId] = deviceDescriptor;
        }
        catch (const cuda_runtime_error& _e)
        {
            cwarn << _e.what();
        }
    }
}

static const uint32_t zero[3] = {0, 0, 0};  // zero the result count

void CUDAMiner::search(
    uint8_t const* header, uint64_t target, uint64_t start_nonce, const dev::eth::WorkPackage& w)
{
    m_done = false;

    set_header(*((hash32_t const*)header));
    if (m_current_target != target)
    {
        set_target(target);
        m_current_target = target;
    }

    // prime each stream, clear search result buffers and start the search
    uint32_t streamIdx;
    for (streamIdx = 0; streamIdx < m_streamSize; streamIdx++, start_nonce += m_batch_size)
    {
        cudaStream_t stream = m_streams[streamIdx];
        HostToDevice((uint8_t*)m_search_buf[streamIdx] + offsetof(Search_results, count), zero,
            sizeof(zero));

        // Run the batch for this stream
        run_ethash_search(m_deviceDescriptor.cuGridSize, m_blockSize, stream,
            m_search_buf[streamIdx], start_nonce);
    }

    // process stream batches until we get new work.

    while (!m_done)
    {
        // Check on every batch if we need to suspend mining
        if (!m_done)
            m_done = paused();

        uint32_t batchCount = 0;

        // This inner loop will process each cuda stream individually
        for (streamIdx = 0; streamIdx < m_streamSize; streamIdx++, start_nonce += m_batch_size)
        {
            // Each pass of this loop will wait for a stream to exit,
            // save any found solutions, then restart the stream
            // on the next group of nonces.
            cudaStream_t stream = m_streams[streamIdx];
            uint8_t* buffer = (uint8_t*)m_search_buf[streamIdx];

            // Wait for the stream complete
            CUDA_CALL(cudaStreamSynchronize(stream));

            if (shouldStop())
                m_done = true;

            struct
            {
                uint32_t foundCount, hashCount;
            } counts;

            DeviceToHost(&counts, buffer + offsetof(Search_results, count), sizeof(counts));

            // clear solution count, hash count and done
            HostToDevice(buffer + offsetof(Search_results, count), zero, sizeof(zero));

            counts.foundCount = min(counts.foundCount, MAX_SEARCH_RESULTS);
            batchCount += counts.hashCount;

            uint32_t gids[MAX_SEARCH_RESULTS];
            h256 mixes[MAX_SEARCH_RESULTS];

            if (counts.foundCount)
            {
                // Extract solution and pass to higer level
                // using io_service as dispatcher

                for (uint32_t i = 0; i < counts.foundCount; i++)
                {
                    DeviceToHost(&gids[i],
                        buffer + offsetof(Search_results, result) + i * sizeof(Search_Result) +
                            offsetof(Search_Result, gid),
                        sizeof(gids[0]));
                    DeviceToHost(mixes[i].data(),
                        buffer + offsetof(Search_results, result) + i * sizeof(Search_Result) +
                            offsetof(Search_Result, mix),
                        sizeof(Search_Result::mix));
                }
                for (uint32_t i = 0; i < m_streamSize; i++)
                    CUDA_CALL(cudaStreamSynchronize(m_streams[i]));
            }

            if (!m_done)
            {
                // restart the stream on the next batch of nonces
                // unless we are done for this round.
                run_ethash_search(m_deviceDescriptor.cuGridSize, m_blockSize, stream,
                    (Search_results*)buffer, start_nonce);
                if (counts.foundCount)
                {
                    uint64_t nonce_base = start_nonce - m_streams_batch_size;
                    for (uint32_t i = 0; i < counts.foundCount; i++)
                    {
                        uint64_t nonce = nonce_base + gids[i];

                        Farm::f().submitProof(
                            Solution{nonce, mixes[i], w, chrono::steady_clock::now(), m_index});
                        cudalog << EthWhite << "Job: " << w.header.abridged()
                                << " Solution: " << toHex(nonce, HexPrefix::Add) << EthReset;
                    }
                }
            }
        }
        updateHashRate(m_blockSize, batchCount);
    }

#ifdef DEV_BUILD
    // Optionally log job switch time
    if (!shouldStop() && (g_logOptions & LOG_SWITCH))
        cudalog << "Switch time: "
                << chrono::duration_cast<chrono::microseconds>(
                       chrono::steady_clock::now() - m_workSwitchStart)
                       .count()
                << " us.";
#endif
}
