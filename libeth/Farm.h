/* Copyright (C) 1883 Thomas Edison - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the GPLv3 license, which unfortunately won't be
 * written for another century.
 *
 * You should have received a copy of the LICENSE file with
 * this file.
 */

#pragma once

#include <atomic>
#include <list>
#include <thread>

#include <boost/asio.hpp>
#include <boost/bind/bind.hpp>
#include <boost/dll.hpp>
#include <boost/filesystem.hpp>
#include <boost/process.hpp>

#include <json/json.h>

#include <libdev/Common.h>
#include <libdev/Worker.h>

#include <libeth/Miner.h>

#include <libhwmon/wrapnvml.h>
#if defined(__linux)
#include <libhwmon/wrapamdsysfs.h>
#include <sys/stat.h>
#else
#include <libhwmon/wrapadl.h>
#endif

using namespace boost::placeholders;

extern boost::asio::io_service g_io_service;

namespace dev {
namespace eth {
struct FarmSettings {
    unsigned hwMon = 0;      // 0 - No monitor; 1 - Temp and Fan; 2 - Temp Fan Power
    unsigned tempStart = 40; // Temperature threshold to restart mining (if paused)
    unsigned tempStop = 0;   // Temperature threshold to pause mining (overheating)
    std::string nonce;
    unsigned cuBlockSize = 0;
    unsigned cuStreams = 0;
    unsigned clGroupSize = 0;
    bool clSplit = false;
};

typedef std::map<string, DeviceDescriptor> minerMap;
typedef std::map<string, int> telemetryMap;

class Farm {
  public:
    unsigned tstart = 0, tstop = 0;

    Farm(minerMap& _DevicesCollection, FarmSettings _settings);

    ~Farm();

    static Farm& f() { return *m_this; }

    void setWork(WorkPackage const& _newWp);
    bool start();
    void stop();
    void pause();
    bool paused();
    void resume();
    void restart();
    void restart_async();
    bool isMining() const { return m_isMining.load(std::memory_order_relaxed); }
    bool reboot(const std::vector<std::string>& args);
    TelemetryType& Telemetry() { return m_telemetry; }
    float HashRate() { return m_telemetry.farm.hashrate; };
    std::vector<std::shared_ptr<Miner>> getMiners() { return m_miners; }
    unsigned getMinersCount() { return (unsigned)m_miners.size(); };

    std::shared_ptr<Miner> getMiner(unsigned index) {
        try {
            return m_miners.at(index);
        } catch (const std::exception&) {
            return nullptr;
        }
    }

    void accountSolution(unsigned _minerIdx, SolutionAccountingEnum _accounting);
    SolutionAccountType& getSolutions();
    SolutionAccountType& getSolutions(unsigned _minerIdx);

    using SolutionFound = std::function<void(const Solution&)>;
    using MinerRestart = std::function<void()>;

    void onSolutionFound(SolutionFound const& _handler) { m_onSolutionFound = _handler; }
    void onMinerRestart(MinerRestart const& _handler) { m_onMinerRestart = _handler; }

    void setTStartTStop(unsigned tstart, unsigned tstop);
    unsigned get_tstart() { return m_Settings.tempStart; }
    unsigned get_tstop() { return m_Settings.tempStop; }
    void submitProof(Solution const& _s);
    void set_nonce(std::string nonce) { m_Settings.nonce = nonce; }
    std::string get_nonce() { return m_Settings.nonce; }

  private:
    std::atomic<bool> m_paused = {false};

    // Async submits solution serializing execution
    // in Farm's strand
    void submitProofAsync(Solution const& _s);

    // Collects data about hashing and hardware status
    void collectData(const boost::system::error_code& ec);

    bool spawn_file_in_bin_dir(const char* filename, const std::vector<std::string>& args);

    mutable std::mutex farmWorkMutex;
    std::vector<std::shared_ptr<Miner>> m_miners; // Collection of miners

    WorkPackage m_currentWp;
    EpochContext m_currentEc;

    std::atomic<bool> m_isMining = {false};

    TelemetryType m_telemetry; // Holds progress and status info for farm and miners

    SolutionFound m_onSolutionFound;
    MinerRestart m_onMinerRestart;

    FarmSettings m_Settings; // Own Farm Settings

    boost::asio::io_service::strand m_io_strand;
    boost::asio::deadline_timer m_collectTimer;
    static const int m_collectInterval = 5000;

    // StartNonce (non-NiceHash Mode) and
    // segment width assigned to each GPU as exponent of 2
    // considering an average block time of 15 seconds
    // a single device GPU should need a speed of 286 Mh/s
    // before it consumes the whole 2^32 segment

    // Wrappers for hardware monitoring libraries and their mappers
    wrap_nvml_handle* nvmlh = nullptr;
    telemetryMap map_nvml_handle = {};

#if defined(__linux)
    wrap_amdsysfs_handle* sysfsh = nullptr;
    std::map<string, int> map_amdsysfs_handle = {};
#else
    wrap_adl_handle* adlh = nullptr;
    std::map<string, int> map_adl_handle = {};
#endif

    static Farm* m_this;
    minerMap& m_DevicesCollection;

    random_device m_engine;
};

} // namespace eth
} // namespace dev
