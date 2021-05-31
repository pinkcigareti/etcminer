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

extern mutex g_seqDAGMutex;
extern bool g_seqDAG;

namespace dev {
namespace eth {
enum class DeviceTypeEnum { Unknown, Cpu, Gpu, Accelerator };

enum class DeviceSubscriptionTypeEnum {
    None,
    OpenCL,
    Cuda,
    Cpu

};

enum class MinerType { Mixed, CL, CUDA, CPU };

enum class HwMonitorInfoType { UNKNOWN, NVIDIA, AMD, CPU };

enum class ClPlatformTypeEnum { Unknown, Amd, Clover, Nvidia, Intel };

enum class SolutionAccountingEnum { Accepted, Rejected, Wasted, Failed };

struct MinerSettings {
    vector<unsigned> devices;
};

struct SolutionAccountType {
    unsigned accepted = 0;
    unsigned rejected = 0;
    unsigned wasted = 0;
    unsigned failed = 0;
    unsigned collectAcceptd = 0;
    std::chrono::steady_clock::time_point tstamp = std::chrono::steady_clock::now();
    string str() {
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

struct HwSensorsType {
    int tempC = 0;
    int memtempC = 0;
    int fanP = 0;
    double powerW = 0.0;
    string str() {
        string _ret = to_string(tempC);
        if (memtempC)
            _ret += '/' + to_string(memtempC);
        _ret += "C " + to_string(fanP) + "%";
        if (powerW)
            _ret.append(" " + boost::str(boost::format("%0.2f") % powerW) + "W");
        return _ret;
    };
};

struct DeviceDescriptor {
    DeviceTypeEnum type = DeviceTypeEnum::Unknown;
    DeviceSubscriptionTypeEnum subscriptionType = DeviceSubscriptionTypeEnum::None;

    string uniqueId;    // For GPUs this is the PCI ID
    size_t totalMemory; // Total memory available by device
    string boardName;

    int cpCpuNumer; // For CPU

    bool cuDetected; // For CUDA detected devices
    unsigned int cuDeviceOrdinal;
    unsigned int cuDeviceIndex;
    string cuCompute;
    unsigned int cuComputeMajor;
    unsigned int cuComputeMinor;
    unsigned int cuBlockSize;
    unsigned int cuStreamSize;

    bool clDetected; // For OpenCL detected devices
    string clPlatformVersion;
    unsigned int clPlatformVersionMajor;
    unsigned int clPlatformVersionMinor;
    unsigned int clDeviceOrdinal;
    unsigned int clDeviceIndex;
    string clDeviceVersion;
    unsigned int clDeviceVersionMajor;
    unsigned int clDeviceVersionMinor;
    string clNvCompute;
    string clArch;
    unsigned int clNvComputeMajor;
    unsigned int clNvComputeMinor;
    unsigned int clPlatformId;
    string clPlatformName;
    ClPlatformTypeEnum clPlatformType = ClPlatformTypeEnum::Unknown;
    unsigned clGroupSize;
    bool clBin;
    bool clSplit;
};

struct HwMonitorInfo {
    HwMonitorInfoType deviceType = HwMonitorInfoType::UNKNOWN;
    string devicePciId;
    int deviceIndex = -1;
};

/// Pause mining
enum MinerPauseEnum {
    PauseDueToOverHeating,
    PauseDueToAPIRequest,
    PauseDueToFarmPaused,
    PauseDueToInsufficientMemory,
    PauseDueToInitEpochError,
    Pause_MAX // Must always be last as a placeholder of max count
};

struct TelemetryAccountType {
    string prefix = "";
    float hashrate = 0.0f;
    bool paused = false;
    HwSensorsType sensors;
    SolutionAccountType solutions;
};

/// Keeps track of progress for farm and miners
struct TelemetryType {
    bool hwmon = false;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();

    TelemetryAccountType farm;
    std::vector<TelemetryAccountType> miners;

    void strvec(std::list<string>& telemetry) {
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
        ss << EthGreen << setw(hoursSize) << hours.count() << ":" << setfill('0') << setw(2) << minutes.count()
           << EthReset << EthWhiteBold << " " << farm.solutions.str() << EthReset << " ";

        const static string suffixes[] = {"h", "Kh", "Mh", "Gh", "Th", "Ph"};
        float hr = farm.hashrate;
        int magnitude = 0;
        while (hr > 1000.0f && magnitude <= 5) {
            hr /= 1000.0f;
            magnitude++;
        }

        ss << EthTealBold << std::fixed << std::setprecision(2) << hr << " " << suffixes[magnitude] << EthReset
           << " - ";
        telemetry.push_back(ss.str());

        int i = -1; // Current miner index
        for (TelemetryAccountType& miner : miners) {
            ss.str("");
            i++;
            hr = miner.hashrate / pow(1000.0f, magnitude);

            ss << (miner.paused || hr < 1 ? EthRed : EthWhite) << miner.prefix << i << " " << EthTeal << std::fixed
               << std::setprecision(2) << hr << EthReset;

            if (hwmon)
                ss << " " << EthTeal << miner.sensors.str() << EthReset;

            // Eventually push also solutions per single GPU
            if (g_logOptions & LOG_PER_GPU)
                ss << " " << EthTeal << miner.solutions.str() << EthReset;

            telemetry.push_back(ss.str());
        }
    };

    std::string str() {
        std::list<string> vs;
        strvec(vs);
        std::string s;
        bool first = true;
        while (!vs.empty()) {
            s += vs.front();
            vs.pop_front();
            if (!vs.empty() && !first)
                s += ", ";
            first = false;
        }
        return s;
    }
};

class Miner : public Worker {
  public:
    Miner(std::string const& _name, unsigned _index) : Worker(_name + std::to_string(_index)), m_index(_index) {}

    ~Miner() override = default;

    DeviceDescriptor getDescriptor();
    void setWork(WorkPackage const& _work);
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
    void setEpoch(WorkPackage const& _newWp);
    void freeCache();

    WorkPackage work() const;
    void ReportSolution(const h256& header, uint64_t nonce);
    void ReportDAGDone(uint64_t dagSize, uint32_t dagTime, bool notSplit);
    void ReportGPUNoMemoryAndPause(std::string mem, uint64_t requiredTotalMemory, uint64_t totalMemory);
    void ReportGPUMemoryRequired(uint32_t lightSize, uint64_t dagSize, uint32_t misc);
    void updateHashRate(uint32_t _groupSize, uint32_t _increment) noexcept;

    const unsigned m_index = 0;          // Ordinal index of the Instance (not the device)
    DeviceDescriptor m_deviceDescriptor; // Info about the device

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

} // namespace eth
} // namespace dev
