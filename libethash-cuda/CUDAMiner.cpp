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

static const unsigned cuPreferedBlockSize = 128;

CUDAMiner::CUDAMiner(unsigned _index, DeviceDescriptor& _device)
  : Miner("cu-", _index),
    m_batch_size(_device.cuGridSize * cuPreferedBlockSize),
    m_streams_batch_size(_device.cuGridSize * cuPreferedBlockSize * 2)
{
    m_deviceDescriptor = _device;
}

CUDAMiner::~CUDAMiner()
{
    stopWorking();
    kick_miner();
}

bool CUDAMiner::initDevice()
{
    cudalog << "Using Pci Id : " << m_deviceDescriptor.uniqueId << " " << m_deviceDescriptor.cuName
            << " (Compute " + m_deviceDescriptor.cuCompute + ") Memory : "
            << dev::getFormattedMemory((double)m_deviceDescriptor.totalMemory);

    // Set Hardware Monitor Info
    m_hwmoninfo.deviceType = HwMonitorInfoType::NVIDIA;
    m_hwmoninfo.devicePciId = m_deviceDescriptor.uniqueId;
    m_hwmoninfo.deviceIndex = -1;  // Will be later on mapped by nvml (see Farm() constructor)

    try
    {
        CUDA_SAFE_CALL(cudaSetDevice(m_deviceDescriptor.cuDeviceIndex));
        CUDA_SAFE_CALL(cudaDeviceReset());
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

bool CUDAMiner::initEpoch_internal()
{
    // If we get here it means epoch has changed so it's not necessary
    // to check again dag sizes. They're changed for sure
    bool retVar = false;
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
            CUDA_SAFE_CALL(cudaDeviceReset());
            CUDA_SAFE_CALL(cudaSetDeviceFlags(cudaDeviceScheduleYield));
            CUDA_SAFE_CALL(cudaDeviceSetCacheConfig(cudaFuncCachePreferL1));

            // Check whether the current device has sufficient memory every time we recreate the dag
            if (m_deviceDescriptor.totalMemory < RequiredTotalMemory)
            {
                if (m_deviceDescriptor.totalMemory < RequiredDagMemory)
                {
                    cudalog << "Epoch " << m_epochContext.epochNumber << " requires "
                            << dev::getFormattedMemory((double)RequiredDagMemory) << " memory.";
                    cudalog << "This device hasn't enough memory available. Mining suspended ...";
                    pause(MinerPauseEnum::PauseDueToInsufficientMemory);
                    return true;  // This will prevent to exit the thread and
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
                CUDA_SAFE_CALL(cudaHostAlloc(reinterpret_cast<void**>(&light),
                    m_epochContext.lightSize, cudaHostAllocDefault));
                cudalog << "WARNING: Generating DAG will take minutes, not seconds";
            }
            else
                CUDA_SAFE_CALL(cudaMalloc(&light, m_epochContext.lightSize));
            m_allocated_memory_light_cache = m_epochContext.lightSize;
            CUDA_SAFE_CALL(cudaMalloc(reinterpret_cast<void**>(&dag), m_epochContext.dagSize));
            m_allocated_memory_dag = m_epochContext.dagSize;

            // create mining buffers
            for (unsigned i = 0; i < 2; ++i)
            {
                CUDA_SAFE_CALL(cudaMalloc(&m_search_buf[i], sizeof(Search_results)));
                CUDA_SAFE_CALL(cudaStreamCreateWithFlags(&m_streams[i], cudaStreamNonBlocking));
            }
            CUDA_SAFE_CALL(cudaMalloc(&m_abort, sizeof(uint32_t)));
        }
        else
        {
            cudalog << "Generating DAG + Light (reusing buffers): "
                    << dev::getFormattedMemory((double)RequiredTotalMemory);
            get_constants(&dag, NULL, &light, NULL);
        }

        CUDA_SAFE_CALL(cudaMemcpy(
            light, m_epochContext.lightCache, m_epochContext.lightSize, cudaMemcpyHostToDevice));

        set_constants(dag, m_epochContext.dagNumItems, light,
            m_epochContext.lightNumItems);  // in ethash_cuda_miner_kernel.cu

        ethash_generate_dag(m_epochContext.dagSize, m_deviceDescriptor.cuGridSize,
            cuPreferedBlockSize, m_streams[0]);

        cudalog << "Generated DAG + Light in "
                << chrono::duration_cast<chrono::milliseconds>(
                       chrono::steady_clock::now() - startInit)
                       .count()
                << " ms. "
                << dev::getFormattedMemory(
                       lightOnHost ? (double)(m_deviceDescriptor.totalMemory - RequiredDagMemory) :
                                     (double)(m_deviceDescriptor.totalMemory - RequiredTotalMemory))
                << " left.";

        retVar = true;
    }
    catch (const cuda_runtime_error& ec)
    {
        cudalog << "Unexpected error " << ec.what() << " on CUDA device "
                << m_deviceDescriptor.uniqueId;
        cudalog << "Mining suspended ...";
        pause(MinerPauseEnum::PauseDueToInitEpochError);
        retVar = true;
    }

    return retVar;
}

void CUDAMiner::workLoop()
{
    WorkPackage current;
    current.header = h256();

    m_search_buf.resize(2);
    m_streams.resize(2);

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
                boost::system_time const timeout =
                    boost::get_system_time() + boost::posix_time::seconds(3);
                boost::mutex::scoped_lock l(x_work);
                m_new_work_signal.timed_wait(l, timeout);
                continue;
            }

            // Epoch change ?
            if (current.epoch != w.epoch)
            {
                if (!initEpoch())
                    break;  // This will simply exit the thread

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

        m_abort = nullptr;
        // Reset miner and stop working
        CUDA_SAFE_CALL(cudaDeviceReset());
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
    m_done = true;
    if (m_abort)
    {
        static uint32_t one = 1;
        CUDA_SAFE_CALL(cudaMemcpyAsync((void*)m_abort, &one, sizeof(one), cudaMemcpyHostToDevice));
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
            CUDA_SAFE_CALL(cudaGetDeviceProperties(&props, i));
            CUDA_SAFE_CALL(cudaMemGetInfo(&freeMem, &totalMem));
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
            deviceDescriptor.cuGridSize = int(ceil(props.multiProcessorCount * 3276.8));

            _DevicesCollection[uniqueId] = deviceDescriptor;
        }
        catch (const cuda_runtime_error& _e)
        {
            cwarn << _e.what();
        }
    }
}

static uint32_t zero2[2] = {0, 0};  // zero the result count

void CUDAMiner::search(
    uint8_t const* header, uint64_t target, uint64_t start_nonce, const dev::eth::WorkPackage& w)
{
    set_header(*reinterpret_cast<hash32_t const*>(header));
    if (m_current_target != target)
    {
        set_target(target);
        m_current_target = target;
    }

    CUDA_SAFE_CALL(cudaMemcpy((uint8_t*)m_abort, zero2, sizeof(zero2[0]), cudaMemcpyHostToDevice));

    // prime each stream, clear search result buffers and start the search
    uint32_t current_index;
    for (current_index = 0; current_index < 2; current_index++, start_nonce += m_batch_size)
    {
        cudaStream_t stream = m_streams[current_index];
        CUDA_SAFE_CALL(
            cudaMemcpy((uint8_t*)m_search_buf[current_index] + offsetof(Search_results, count),
                zero2, sizeof(zero2), cudaMemcpyHostToDevice));

        // Run the batch for this stream
        run_ethash_search(m_deviceDescriptor.cuGridSize, cuPreferedBlockSize, stream,
            m_search_buf[current_index], m_abort, start_nonce);
    }

    // process stream batches until we get new work.
    m_done = false;

    while (!m_done)
    {
        // Check on every batch if we need to suspend mining
        if (!m_done)
            m_done = paused();

        uint32_t batchCount = 0;

        // This inner loop will process each cuda stream individually
        for (current_index = 0; current_index < 2; current_index++, start_nonce += m_batch_size)
        {
            // Each pass of this loop will wait for a stream to exit,
            // save any found solutions, then restart the stream
            // on the next group of nonces.
            cudaStream_t stream = m_streams[current_index];
            uint8_t* buffer = (uint8_t*)m_search_buf[current_index];

            // Wait for the stream complete
            CUDA_SAFE_CALL(cudaStreamSynchronize(stream));

            uint32_t hashCount;
            CUDA_SAFE_CALL(cudaMemcpy(&hashCount, buffer + offsetof(Search_results, hashCount),
                sizeof(hashCount), cudaMemcpyDeviceToHost));
            CUDA_SAFE_CALL(cudaMemcpy(buffer + offsetof(Search_results, hashCount), zero2,
                sizeof(hashCount), cudaMemcpyHostToDevice));

            batchCount += hashCount;

            if (shouldStop())
                m_done = true;

            // Detect solutions in current stream's solution buffer
            uint32_t found_count;
            CUDA_SAFE_CALL(cudaMemcpy(&found_count, buffer + offsetof(Search_results, count),
                sizeof(found_count), cudaMemcpyDeviceToHost));
            found_count = min(found_count, MAX_SEARCH_RESULTS);

            uint32_t gids[MAX_SEARCH_RESULTS];
            h256 mixes[MAX_SEARCH_RESULTS];

            if (found_count)
            {
                CUDA_SAFE_CALL(cudaMemcpy(buffer + offsetof(Search_results, count), zero2,
                    sizeof(zero2[0]), cudaMemcpyHostToDevice));

                // Extract solution and pass to higer level
                // using io_service as dispatcher

                for (uint32_t i = 0; i < found_count; i++)
                {
                    CUDA_SAFE_CALL(cudaMemcpy(gids + i,
                        buffer + offsetof(Search_results, result) + i * sizeof(Search_Result) +
                            offsetof(Search_Result, gid),
                        sizeof(gids[0]), cudaMemcpyDeviceToHost));
                    CUDA_SAFE_CALL(cudaMemcpy(mixes[i].data(),
                        buffer + offsetof(Search_results, result) + i * sizeof(Search_Result) +
                            offsetof(Search_Result, mix),
                        sizeof(Search_Result::mix), cudaMemcpyDeviceToHost));
                }
            }

            // restart the stream on the next batch of nonces
            // unless we are done for this round.
            if (!m_done)
                run_ethash_search(m_deviceDescriptor.cuGridSize, cuPreferedBlockSize, stream,
                    (Search_results*)buffer, m_abort, start_nonce);

            if (found_count)
            {
                uint64_t nonce_base = start_nonce - m_streams_batch_size;
                for (uint32_t i = 0; i < found_count; i++)
                {
                    uint64_t nonce = nonce_base + gids[i];

                    Farm::f().submitProof(
                        Solution{nonce, mixes[i], w, chrono::steady_clock::now(), m_index});
                    cudalog << EthWhite << "Job: " << w.header.abridged() << " Sol: 0x"
                            << toHex(nonce) << EthReset;
                }
            }
        }
        updateHashRate(cuPreferedBlockSize, batchCount);

        // Bail out if it's shutdown time
        if (shouldStop())
            break;
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
