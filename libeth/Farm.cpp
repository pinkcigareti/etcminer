
/* Copyright (C) 1883 Thomas Edison - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the GPLv3 license, which unfortunately won't be
 * written for another century.
 *
 * You should have received a copy of the LICENSE file with
 * this file.
 */

#include <libeth/Farm.h>

#if ETH_ETHASHCL
#include <libcl/CLMiner.h>
#endif

#if ETH_ETHASHCUDA
#include <libcuda/CUDAMiner.h>
#endif

#if ETH_ETHASHCPU
#include <libcpu/CPUMiner.h>
#endif

#include <libpool/PoolManager.h>

namespace dev {
namespace eth {
Farm* Farm::m_this = nullptr;
const int Farm::m_collectInterval;

Farm::Farm(minerMap& _DevicesCollection, FarmSettings _settings)
    : m_Settings(move(_settings)), m_io_strand(g_io_service), m_collectTimer(g_io_service),
      m_DevicesCollection(_DevicesCollection) {
    m_this = this;
    // Init HWMON if needed
    if (m_Settings.hwMon) {
        m_telemetry.hwmon = true;

#if defined(__linux)
        bool need_sysfsh = false;
#else
        bool need_adlh = false;
#endif
        bool need_nvmlh = false;

        // Scan devices collection to identify which hw monitors to initialize
        for (auto it = m_DevicesCollection.begin(); it != m_DevicesCollection.end(); it++) {
            if (it->second.subscriptionType == DeviceSubscriptionTypeEnum::Cuda) {
                need_nvmlh = true;
                continue;
            }
            if (it->second.subscriptionType == DeviceSubscriptionTypeEnum::OpenCL) {
                if (it->second.clPlatformType == ClPlatformTypeEnum::Nvidia) {
                    need_nvmlh = true;
                    continue;
                }
                if (it->second.clPlatformType == ClPlatformTypeEnum::Amd) {
#if defined(__linux)
                    need_sysfsh = true;
#else
                    need_adlh = true;
#endif
                    continue;
                }
            }
        }

#if defined(__linux)
        if (need_sysfsh)
            sysfsh = wrap_amdsysfs_create();
        if (sysfsh) {
            // Build Pci identification mapping as done in miners.
            for (int i = 0; i < sysfsh->sysfs_gpucount; i++) {
                ostringstream oss;
                string uniqueId;
                oss << setfill('0') << setw(4) << hex << (unsigned)sysfsh->sysfs_pci_domain_id[i] << ':' << setw(2)
                    << (unsigned)sysfsh->sysfs_pci_bus_id[i] << ":" << setw(2)
                    << (unsigned int)(sysfsh->sysfs_pci_device_id[i]) << ".0";
                uniqueId = oss.str();
                map_amdsysfs_handle[uniqueId] = i;
            }
        }

#else
        if (need_adlh)
            adlh = wrap_adl_create();
        if (adlh) {
            // Build Pci identification as done in miners.
            for (int i = 0; i < adlh->adl_gpucount; i++) {
                ostringstream oss;
                string uniqueId;
                oss << "0000:" << setfill('0') << setw(2) << hex
                    << (unsigned int)adlh->devs[adlh->phys_logi_device_id[i]].iBusNumber << ":" << setw(2)
                    << (unsigned int)(adlh->devs[adlh->phys_logi_device_id[i]].iDeviceNumber) << ".0";
                uniqueId = oss.str();
                map_adl_handle[uniqueId] = i;
            }
        }

#endif
        if (need_nvmlh)
            nvmlh = wrap_nvml_create();
        if (nvmlh) {
            // Build Pci identification as done in miners.
            for (int i = 0; i < nvmlh->nvml_gpucount; i++) {
                ostringstream oss;
                string uniqueId;
                oss << setfill('0') << setw(4) << hex << (unsigned)nvmlh->nvml_pci_domain_id[i] << ':' << setw(2) << hex
                    << (unsigned)nvmlh->nvml_pci_bus_id[i] << ":" << setw(2)
                    << (unsigned int)(nvmlh->nvml_pci_device_id[i] >> 3) << ".0";
                uniqueId = oss.str();
                map_nvml_handle[uniqueId] = i;
            }
        }
    }

    // Start data collector timer
    // It should work for the whole lifetime of Farm
    // regardless it's mining state
    m_collectTimer.expires_from_now(boost::posix_time::milliseconds(m_collectInterval));
    m_collectTimer.async_wait(
        m_io_strand.wrap(boost::bind(&Farm::collectData, this, boost::asio::placeholders::error)));
}

Farm::~Farm() {
    // Stop data collector (before monitors !!!)
    m_collectTimer.cancel();

    // Deinit HWMON
#if defined(__linux)
    if (sysfsh)
        wrap_amdsysfs_destroy(sysfsh);
#else
    if (adlh)
        wrap_adl_destroy(adlh);
#endif
    if (nvmlh)
        wrap_nvml_destroy(nvmlh);

    // Stop mining (if needed)
    if (m_isMining.load(memory_order_relaxed))
        stop();
}

void Farm::setWork(WorkPackage const& _newWp) {
    // Set work to each miner giving it's own starting nonce
    unique_lock<mutex> l(farmWorkMutex);

    m_currentWp = _newWp;

    // Get the randomly selected nonce
    uint16_t segmentBits(64 - (unsigned)ceil(log2(m_miners.size())));
    if (m_Settings.nonce.size()) {
        segmentBits -= 4 * m_Settings.nonce.size();
        m_currentWp.startNonce = strtoull(m_Settings.nonce.c_str(), nullptr, 16)
                                 << (64 - (4 * m_Settings.nonce.size()));
    } else {
        if (m_currentWp.exSizeBytes > 0) {
            // Equally divide the residual segment among miners
            segmentBits -= m_currentWp.exSizeBytes * 4;
        } else
            m_currentWp.startNonce = uniform_int_distribution<uint64_t>()(m_engine);
    }

    for (unsigned int i = 0; i < m_miners.size(); i++) {
        m_miners.at(i)->setWork(m_currentWp);
        m_currentWp.startNonce += 1ULL << segmentBits;
    }
}

/**
 * @brief Start a number of miners.
 */
bool Farm::start() {
    // Prevent recursion
    if (m_isMining.load(memory_order_relaxed))
        return true;

    unique_lock<mutex> l(farmWorkMutex);

    // Start all subscribed miners if none yet
    if (!m_miners.size()) {
        for (auto it = m_DevicesCollection.begin(); it != m_DevicesCollection.end(); it++) {
            TelemetryAccountType minerTelemetry;
#if ETH_ETHASHCUDA
            if (it->second.subscriptionType == DeviceSubscriptionTypeEnum::Cuda) {
                minerTelemetry.prefix = "cu";
                if (m_Settings.cuBlockSize)
                    it->second.cuBlockSize = m_Settings.cuBlockSize;
                if (m_Settings.cuStreams)
                    it->second.cuStreamSize = m_Settings.cuStreams;
                m_miners.push_back(shared_ptr<Miner>(new CUDAMiner(m_miners.size(), it->second)));
            }
#endif
#if ETH_ETHASHCL

            if (it->second.subscriptionType == DeviceSubscriptionTypeEnum::OpenCL) {
                minerTelemetry.prefix = "cl";
                if (m_Settings.clGroupSize)
                    it->second.clGroupSize = m_Settings.clGroupSize;
                it->second.clSplit = m_Settings.clSplit;
                m_miners.push_back(shared_ptr<Miner>(new CLMiner(m_miners.size(), it->second)));
            }
#endif
#if ETH_ETHASHCPU

            if (it->second.subscriptionType == DeviceSubscriptionTypeEnum::Cpu) {
                minerTelemetry.prefix = "cp";
                m_miners.push_back(shared_ptr<Miner>(new CPUMiner(m_miners.size(), it->second)));
            }
#endif
            if (minerTelemetry.prefix.empty())
                continue;
            m_telemetry.miners.push_back(minerTelemetry);
            m_miners.back()->startWorking();
        }

    } else
        for (auto const& miner : m_miners)
            miner->startWorking();

    m_isMining.store(true, memory_order_relaxed);

    return m_isMining.load(memory_order_relaxed);
}

/**
 * @brief Stop all mining activities.
 */
void Farm::stop() {
    // Avoid re-entering if not actually mining.
    // This, in fact, is also called by destructor
    if (isMining()) {
        {
            unique_lock<mutex> l(farmWorkMutex);
            for (auto const& miner : m_miners) {
                miner->triggerStopWorking();
                miner->kick_miner();
            }
            m_miners.clear();
            m_telemetry.miners.clear();
            m_isMining.store(false, memory_order_relaxed);
        }
    }
}

/**
 * @brief Pauses the whole collection of miners
 */
void Farm::pause() {
    // Signal each miner to suspend mining
    unique_lock<mutex> l(farmWorkMutex);
    m_paused.store(true, memory_order_relaxed);
    for (auto const& m : m_miners)
        m->pause(MinerPauseEnum::PauseDueToFarmPaused);
}

/**
 * @brief Returns whether or not this farm is paused for any reason
 */
bool Farm::paused() { return m_paused.load(memory_order_relaxed); }

/**
 * @brief Resumes from a pause condition
 */
void Farm::resume() {
    // Signal each miner to resume mining
    // Note ! Miners may stay suspended if other reasons
    unique_lock<mutex> l(farmWorkMutex);
    m_paused.store(false, memory_order_relaxed);
    for (auto const& m : m_miners)
        m->resume(MinerPauseEnum::PauseDueToFarmPaused);
}

/**
 * @brief Stop all mining activities and Starts them again
 */
void Farm::restart() {
    if (m_onMinerRestart)
        m_onMinerRestart();
}

/**
 * @brief Stop all mining activities and Starts them again (async post)
 */
void Farm::restart_async() { g_io_service.post(m_io_strand.wrap(boost::bind(&Farm::restart, this))); }

/**
 * @brief Spawn a reboot script (reboot.bat/reboot.sh)
 * @return false if no matching file was found
 */
bool Farm::reboot(const vector<string>& args) {
#if defined(_WIN32)
    const char* filename = "reboot.bat";
#else
    const char* filename = "reboot.sh";
#endif

    return spawn_file_in_bin_dir(filename, args);
}

/**
 * @brief Account solutions for miner and for farm
 */
void Farm::accountSolution(unsigned _minerIdx, SolutionAccountingEnum _accounting) {
    m_telemetry.farm.solutions.tstamp = chrono::steady_clock::now();
    if (_accounting == SolutionAccountingEnum::Accepted) {
        m_telemetry.farm.solutions.accepted++;
        atomic_fetch_add((atomic<unsigned>*)&m_telemetry.farm.solutions.collectAcceptd, 1u);
        m_telemetry.miners.at(_minerIdx).solutions.accepted++;
        atomic_fetch_add((atomic<unsigned>*)&m_telemetry.miners.at(_minerIdx).solutions.collectAcceptd, 1u);
    } else if (_accounting == SolutionAccountingEnum::Wasted) {
        m_telemetry.farm.solutions.wasted++;
        m_telemetry.miners.at(_minerIdx).solutions.wasted++;
    } else if (_accounting == SolutionAccountingEnum::Rejected) {
        m_telemetry.farm.solutions.rejected++;
        m_telemetry.miners.at(_minerIdx).solutions.rejected++;
    } else if (_accounting == SolutionAccountingEnum::Failed) {
        m_telemetry.farm.solutions.failed++;
        m_telemetry.miners.at(_minerIdx).solutions.failed++;
    }
    m_telemetry.miners.at(_minerIdx).solutions.tstamp = chrono::steady_clock::now();
}

/**
 * @brief Gets the solutions account for the whole farm
 */

SolutionAccountType& Farm::getSolutions() { return m_telemetry.farm.solutions; }

/**
 * @brief Gets the solutions account for single miner
 */
SolutionAccountType& Farm::getSolutions(unsigned _minerIdx) {
    static SolutionAccountType nullSolAccount;
    try {
        return m_telemetry.miners.at(_minerIdx).solutions;
    } catch (const exception&) {
        return nullSolAccount;
    }
}

void Farm::setTStartTStop(unsigned tstart, unsigned tstop) {
    m_Settings.tempStart = tstart;
    m_Settings.tempStop = tstop;
}

void Farm::submitProof(Solution const& _s) {
    g_io_service.post(m_io_strand.wrap(boost::bind(&Farm::submitProofAsync, this, _s)));
}

void Farm::submitProofAsync(Solution const& _s) {
    Result r = EthashAux::eval(_s.work.epoch, _s.work.header, _s.nonce);
    if (r.value > _s.work.boundary) {
        accountSolution(_s.midx, SolutionAccountingEnum::Failed);
        cwarn << "GPU " << _s.midx << " gave incorrect result. Lower overclocking values if it happens frequently.";
        return;
    }
    m_onSolutionFound(Solution{_s.nonce, r.mixHash, _s.work, _s.tstamp, _s.midx});

#ifdef DEV_BUILD
    if (g_logOptions & LOG_SUBMIT)
        cnote << "Submit time: "
              << chrono::duration_cast<chrono::microseconds>(chrono::steady_clock::now() - _s.tstamp).count() << " us.";
#endif
    if (_s.nonce)
        cnote << EthWhite "Solution difficulty: "
              << dev::getFormattedHashes(dev::getHashesToTarget(r.value.hex(HexPrefix::Add)));
}

// Collects data about hashing and hardware status
void Farm::collectData(const boost::system::error_code& ec) {
    if (ec)
        return;

    // check for hung miners
    for (auto const& miner : m_miners)
        if (!miner->paused() && miner->m_initialized) {
            if (miner->m_hung_miner.load()) {
                if (g_exitOnError)
                    throw runtime_error("Hung GPU");
                else if (!reboot({{"hung_miner_reboot"}}))
                    cwarn << "Hung GPU " << miner->Index() << " detected and reboot script failed!";
                return;
            }
            miner->m_hung_miner.store(true);
        }

    // Reset hashrate (it will accumulate from miners)
    float farm_hr = 0.0f;

    // Process miners
    for (auto const& miner : m_miners) {
        int minerIdx = miner->Index();
        float hr = (miner->paused() ? 0.0f : miner->RetrieveHashRate());
        farm_hr += hr;
        m_telemetry.miners.at(minerIdx).hashrate = hr;
        m_telemetry.miners.at(minerIdx).paused = miner->paused();

        if (m_Settings.hwMon) {
            HwMonitorInfo hwInfo = miner->hwmonInfo();

            unsigned int tempC = 0, memtempC = 0, fanpcnt = 0, powerW = 0;

            if (hwInfo.deviceType == HwMonitorInfoType::NVIDIA && nvmlh) {
                int devIdx = hwInfo.deviceIndex;
                if (devIdx == -1 && !hwInfo.devicePciId.empty()) {
                    if (map_nvml_handle.find(hwInfo.devicePciId) != map_nvml_handle.end()) {
                        devIdx = map_nvml_handle[hwInfo.devicePciId];
                        miner->setHwmonDeviceIndex(devIdx);
                    } else {
                        // This will prevent further tries to map
                        miner->setHwmonDeviceIndex(-2);
                    }
                }

                if (devIdx >= 0) {
                    wrap_nvml_get_tempC(nvmlh, devIdx, &tempC);
                    wrap_nvml_get_mem_tempC(nvmlh, devIdx, &memtempC);
                    wrap_nvml_get_fanpcnt(nvmlh, devIdx, &fanpcnt);

                    if (m_Settings.hwMon == 2)
                        wrap_nvml_get_power_usage(nvmlh, devIdx, &powerW);
                }
            } else if (hwInfo.deviceType == HwMonitorInfoType::AMD) {
#if defined(__linux)
                if (sysfsh) {
                    int devIdx = hwInfo.deviceIndex;
                    if (devIdx == -1 && !hwInfo.devicePciId.empty()) {
                        if (map_amdsysfs_handle.find(hwInfo.devicePciId) != map_amdsysfs_handle.end()) {
                            devIdx = map_amdsysfs_handle[hwInfo.devicePciId];
                            miner->setHwmonDeviceIndex(devIdx);
                        } else {
                            // This will prevent further tries to map
                            miner->setHwmonDeviceIndex(-2);
                        }
                    }

                    if (devIdx >= 0) {
                        wrap_amdsysfs_get_tempC(sysfsh, devIdx, &tempC);
                        wrap_amdsysfs_get_mem_tempC(sysfsh, devIdx, &memtempC);
                        wrap_amdsysfs_get_fanpcnt(sysfsh, devIdx, &fanpcnt);

                        if (m_Settings.hwMon == 2)
                            wrap_amdsysfs_get_power_usage(sysfsh, devIdx, &powerW);
                    }
                }
#else
                if (adlh) // Windows only for AMD
                {
                    int devIdx = hwInfo.deviceIndex;
                    if (devIdx == -1 && !hwInfo.devicePciId.empty()) {
                        if (map_adl_handle.find(hwInfo.devicePciId) != map_adl_handle.end()) {
                            devIdx = map_adl_handle[hwInfo.devicePciId];
                            miner->setHwmonDeviceIndex(devIdx);
                        } else {
                            // This will prevent further tries to map
                            miner->setHwmonDeviceIndex(-2);
                        }
                    }

                    if (devIdx >= 0) {
                        wrap_adl_get_tempC(adlh, devIdx, &tempC);
                        wrap_adl_get_mem_tempC(adlh, devIdx, &memtempC);
                        wrap_adl_get_fanpcnt(adlh, devIdx, &fanpcnt);

                        if (m_Settings.hwMon == 2)
                            wrap_adl_get_power_usage(adlh, devIdx, &powerW);
                    }
                }
#endif
            }

            // If temperature control has been enabled call
            // check threshold
            if (m_Settings.tempStop) {
                bool paused = miner->pauseTest(MinerPauseEnum::PauseDueToOverHeating);
                if (!paused && (tempC >= m_Settings.tempStop))
                    miner->pause(MinerPauseEnum::PauseDueToOverHeating);
                if (paused && (tempC <= m_Settings.tempStart))
                    miner->resume(MinerPauseEnum::PauseDueToOverHeating);
            }

            m_telemetry.miners.at(minerIdx).sensors.tempC = tempC;
            m_telemetry.miners.at(minerIdx).sensors.memtempC = memtempC;
            m_telemetry.miners.at(minerIdx).sensors.fanP = fanpcnt;
            m_telemetry.miners.at(minerIdx).sensors.powerW = powerW / ((double)1000.0);
        }
        m_telemetry.farm.hashrate = farm_hr;
        miner->TriggerHashRateUpdate();
    }

    // Resubmit timer for another loop
    m_collectTimer.expires_from_now(boost::posix_time::milliseconds(m_collectInterval));
    m_collectTimer.async_wait(
        m_io_strand.wrap(boost::bind(&Farm::collectData, this, boost::asio::placeholders::error)));
}

bool Farm::spawn_file_in_bin_dir(const char* filename, const vector<string>& args) {
    string fn = boost::dll::program_location().parent_path().string() +
                "/" + // boost::filesystem::path::preferred_separator
                filename;
    try {
        if (!boost::filesystem::exists(fn))
            return false;

        /* anything in the file */
        if (!boost::filesystem::file_size(fn))
            return false;

#if defined(__linux)
        struct stat sb;
        if (stat(fn.c_str(), &sb) != 0)
            return false;
        /* just check if any exec flag is set.
           still execution can fail (not the uid, not in the group, selinux, ...)
         */
        if ((sb.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)) == 0)
            return false;
#endif
        /* spawn it (no wait,...) - fire and forget! */
        boost::process::spawn(fn, args);
        return true;
    } catch (...) {
    }
    return false;
}

} // namespace eth
} // namespace dev
