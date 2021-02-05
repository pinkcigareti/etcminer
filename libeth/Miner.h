/* Copyright (C) 1883 Thomas Edison - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the GPLv3 license, which unfortunately won't be
 * written for another century.
 *
 * You should have received a copy of the LICENSE file with
 * this file.
 */

#pragma once

#include <bitset>
#include <condition_variable>
#include <list>
#include <mutex>
#include <numeric>
#include <string>

#include "EthashAux.h"
#include <libdev/Common.h>
#include <libdev/Log.h>
#include <libdev/Worker.h>

#include <boost/format.hpp>

using namespace std;

namespace dev
{
namespace eth
{
enum class DeviceTypeEnum
{
    Unknown,
    Cpu,
    Gpu,
    Accelerator
};

enum class DeviceSubscriptionTypeEnum
{
    None,
    OpenCL,
    Cuda,
    Cpu

};

enum class MinerType
{
    Mixed,
    CL,
    CUDA,
    CPU
};

enum class HwMonitorInfoType
{
    UNKNOWN,
    NVIDIA,
    AMD,
    CPU
};

enum class ClPlatformTypeEnum
{
    Unknown,
    Amd,
    Clover,
    Nvidia,
    Intel
};

enum class SolutionAccountingEnum
{
    Accepted,
    Rejected,
    Wasted,
    Failed
};

struct MinerSettings
{
    vector<unsigned> devices;
};

struct SolutionAccountType
{
    unsigned accepted = 0;
    unsigned rejected = 0;
    unsigned wasted = 0;
    unsigned failed = 0;
    std::chrono::steady_clock::time_point tstamp = std::chrono::steady_clock::now();
    string str()
    {
        string _ret = "A" + to_string(accepted);
        if (wasted)
            _ret.append(":W" + to_string(wasted));
        if (rejected)
            _ret.append(":R" + to_string(rejected));
        if (failed)
            _ret.append(":F" + to_string(failed));
        return _ret;
    };
};

struct HwSensorsType
{
    int tempC = 0;
    int fanP = 0;
    double powerW = 0.0;
    string str()
    {
        string _ret = to_string(tempC) + "C " + to_string(fanP) + "%";
        if (powerW)
            _ret.append(" " + boost::str(boost::format("%0.2f") % powerW) + "W");
        return _ret;
    };
};

struct DeviceDescriptor
{
    DeviceTypeEnum type = DeviceTypeEnum::Unknown;
    DeviceSubscriptionTypeEnum subscriptionType = DeviceSubscriptionTypeEnum::None;

    string uniqueId;     // For GPUs this is the PCI ID
    size_t totalMemory;  // Total memory available by device
    string name;         // Device Name

    int cpCpuNumer;  // For CPU

    bool cuDetected;  // For CUDA detected devices
    string cuName;
    unsigned int cuDeviceOrdinal;
    unsigned int cuDeviceIndex;
    string cuCompute;
    unsigned int cuComputeMajor;
    unsigned int cuComputeMinor;
    unsigned int cuBlockSize;
    unsigned int cuStreamSize;

    bool clDetected;  // For OpenCL detected devices
    string clPlatformVersion;
    unsigned int clPlatformVersionMajor;
    unsigned int clPlatformVersionMinor;
    unsigned int clDeviceOrdinal;
    unsigned int clDeviceIndex;
    string clDeviceVersion;
    unsigned int clDeviceVersionMajor;
    unsigned int clDeviceVersionMinor;
    string clBoardName;
    string clNvCompute;
    unsigned int clNvComputeMajor;
    unsigned int clNvComputeMinor;
    string clName;
    unsigned int clPlatformId;
    string clPlatformName;
    ClPlatformTypeEnum clPlatformType = ClPlatformTypeEnum::Unknown;
    unsigned clGroupSize;
    bool clBin;
};

struct HwMonitorInfo
{
    HwMonitorInfoType deviceType = HwMonitorInfoType::UNKNOWN;
    string devicePciId;
    int deviceIndex = -1;
};

/// Pause mining
enum MinerPauseEnum
{
    PauseDueToOverHeating,
    PauseDueToAPIRequest,
    PauseDueToFarmPaused,
    PauseDueToInsufficientMemory,
    PauseDueToInitEpochError,
    Pause_MAX  // Must always be last as a placeholder of max count
};

struct TelemetryAccountType
{
    string prefix = "";
    float hashrate = 0.0f;
    bool paused = false;
    HwSensorsType sensors;
    SolutionAccountType solutions;
    double effectiveHashRate = 0;
};

/// Keeps track of progress for farm and miners
struct TelemetryType
{
    bool hwmon = false;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();

    TelemetryAccountType farm;
    std::vector<TelemetryAccountType> miners;

    void strvec(std::list<string>& telemetry)
    {
        std::stringstream ss;

        /*

        Output is formatted as

        Run <h:mm> <Solutions> <Speed> [<miner> ...]
        where
        - Run h:mm    Duration of the batch
        - Solutions   Detailed solutions (A+R+F) per farm
        - Speed       Actual hashing rate

        each <miner> reports
        - speed       Actual speed at the same level of
                      magnitude for farm speed
        - sensors     Values of sensors (temp, fan, power)
        - solutions   Optional (LOG_PER_GPU) Solutions detail per GPU
        */

        /*
        Calculate duration
        */
        auto duration = std::chrono::steady_clock::now() - start;
        auto hours = std::chrono::duration_cast<std::chrono::hours>(duration);
        int hoursSize = (hours.count() > 9 ? (hours.count() > 99 ? 3 : 2) : 1);
        duration -= hours;
        auto minutes = std::chrono::duration_cast<std::chrono::minutes>(duration);
        ss << EthGreen << setw(hoursSize) << hours.count() << ":" << setfill('0') << setw(2)
           << minutes.count() << EthReset << EthWhiteBold << " " << farm.solutions.str() << EthReset
           << " ";

        const static string suffixes[] = {"h", "Kh", "Mh", "Gh"};
        float hr = farm.hashrate;
        float ehr;
        int magnitude = 0;
        while (hr > 1000.0f && magnitude <= 3)
        {
            hr /= 1000.0f;
            magnitude++;
        }

        ss << EthTealBold << std::fixed << std::setprecision(2) << hr << " " << suffixes[magnitude]
           << EthReset << " - ";
        telemetry.push_back(ss.str());

        int i = -1;  // Current miner index
        for (TelemetryAccountType miner : miners)
        {
            ss.str("");
            i++;
            hr = miner.hashrate;
            if (hr > 0.0f)
                hr /= pow(1000.0f, magnitude);

            if (g_logOptions & LOG_EFFECTIVE)
            {
                ehr = miner.effectiveHashRate;
                while (ehr > 1000.0)
                    ehr /= 1000.0f;

                ss << (miner.paused || hr < 1 ? EthRed : EthWhite) << miner.prefix << i << " "
                   << EthTeal << std::fixed << std::setprecision(2) << hr << '(' << std::fixed
                   << ehr << ")" EthReset;
            }
            else
                ss << (miner.paused || hr < 1 ? EthRed : EthWhite) << miner.prefix << i << " "
                   << EthTeal << std::fixed << std::setprecision(2) << hr << EthReset;

            if (hwmon)
                ss << " " << EthTeal << miner.sensors.str() << EthReset;

            // Eventually push also solutions per single GPU
            if (g_logOptions & LOG_PER_GPU)
                ss << " " << EthTeal << miner.solutions.str() << EthReset;

            telemetry.push_back(ss.str());
        }
    };

    std::string str()
    {
        std::list<string> vs;
        strvec(vs);
        std::string s(vs.front());
        vs.pop_front();
        while (vs.size() != 1)
        {
            s += vs.front() + ", ";
            vs.pop_front();
        }
        return s + vs.front();
    }
};


class FarmFace
{
public:
    FarmFace() { m_this = this; }
    static FarmFace& f() { return *m_this; };

    virtual ~FarmFace() = default;
    virtual unsigned get_tstart() = 0;
    virtual unsigned get_tstop() = 0;

    /**
     * @brief Called from a Miner to note a WorkPackage has a solution.
     * @param _p The solution.
     * @return true iff the solution was good (implying that mining should be .
     */
    virtual void submitProof(Solution const& _p) = 0;
    virtual void accountSolution(unsigned _minerIdx, SolutionAccountingEnum _accounting) = 0;

private:
    static FarmFace* m_this;
};

class Miner : public Worker
{
public:
    Miner(std::string const& _name, unsigned _index)
      : Worker(_name + std::to_string(_index)), m_index(_index)
    {}

    ~Miner() override = default;

    DeviceDescriptor getDescriptor();
    void setWork(WorkPackage const& _work);
    void setEpoch(EpochContext const& _ec) { m_epochContext = _ec; }
    unsigned Index() { return m_index; };
    HwMonitorInfo hwmonInfo() { return m_hwmoninfo; }
    void setHwmonDeviceIndex(int i) { m_hwmoninfo.deviceIndex = i; }
    virtual void kick_miner() = 0;
    void pause(MinerPauseEnum what);
    bool paused();
    bool pauseTest(MinerPauseEnum what);
    std::string pausedString();
    void resume(MinerPauseEnum fromwhat);
    float RetrieveHashRate() noexcept;
    void TriggerHashRateUpdate() noexcept;

    std::atomic<bool> m_hung_miner = {false};
    bool m_initialized = false;

protected:
    virtual bool initDevice() = 0;
    virtual bool initEpoch() = 0;

    WorkPackage work() const;
    void ReportSolution(const h256& header, uint64_t nonce);
    void ReportDAGDone(uint64_t dagSize, uint32_t dagTime);
    void ReportGPUMemoryUsage(uint64_t requiredTotalMemory, uint64_t totalMemory);
    void ReportGPUNoMemoryAndPause(uint64_t requiredTotalMemory, uint64_t totalMemory);
    void updateHashRate(uint32_t _groupSize, uint32_t _increment) noexcept;

    const unsigned m_index = 0;           // Ordinal index of the Instance (not the device)
    DeviceDescriptor m_deviceDescriptor;  // Info about the device

    EpochContext m_epochContext;

#ifdef DEV_BUILD
    std::chrono::steady_clock::time_point m_workSwitchStart;
#endif

    HwMonitorInfo m_hwmoninfo;
    mutable std::mutex miner_work_mutex;
    mutable std::mutex x_pause;
    std::condition_variable m_new_work_signal;

    uint32_t m_block_multiple;

private:
    bitset<MinerPauseEnum::Pause_MAX> m_pauseFlags;

    WorkPackage m_work;

    std::chrono::steady_clock::time_point m_hashTime = std::chrono::steady_clock::now();
    std::atomic<float> m_hashRate = {0.0};
    atomic<bool> m_hashRateUpdate = {false};
    uint64_t m_groupCount = 0;
};

}  // namespace eth
}  // namespace dev
