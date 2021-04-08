
/* Copyright (C) 1883 Thomas Edison - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the GPLv3 license, which unfortunately won't be
 * written for another century.
 *
 * You should have received a copy of the LICENSE file with
 * this file.
 */

#include "libpool/PoolManager.h"

#include "Miner.h"

namespace dev {
namespace eth {

DeviceDescriptor Miner::getDescriptor() { return m_deviceDescriptor; }

void Miner::setWork(WorkPackage const& _work) {
    {
        lock_guard<mutex> l(miner_work_mutex);

        // Void work if this miner is paused
        if (paused())
            m_work.header = h256();
        else
            m_work = _work;
#ifdef DEV_BUILD
        m_workSwitchStart = chrono::steady_clock::now();
#endif
    }
    kick_miner();
}

void Miner::ReportSolution(const h256& header, uint64_t nonce) {
    cnote << EthWhite << "Job: " << header.abridged() << " Solution: " << toHex(nonce, HexPrefix::Add);
}

void Miner::ReportDAGDone(uint64_t dagSize, uint32_t dagTime, bool notSplit) {
    cextr << dev::getFormattedMemory(float(dagSize)) << " of " << (notSplit ? "" : "(split) ")
          << "DAG data generated in " << fixed << setprecision(1) << dagTime / 1000.0f << " seconds";
}

void Miner::ReportGPUMemoryRequired(uint32_t lightSize, uint64_t dagSize, uint32_t misc) {
    cextr << "Required GPU mem: Total " << dev::getFormattedMemory(float(lightSize + dagSize + misc)) << ", Cache "
          << dev::getFormattedMemory(float(lightSize)) << ", DAG " << dev::getFormattedMemory(float(dagSize))
          << ", Miscellaneous " << dev::getFormattedMemory(float(misc));
}

void Miner::ReportGPUNoMemoryAndPause(string mem, uint64_t requiredMemory, uint64_t totalMemory) {
    cwarn << "Epoch " << m_epochContext.epochNumber << " requires " << dev::getFormattedMemory((double)requiredMemory)
          << " of " << mem << " memory from total of " << dev::getFormattedMemory((double)totalMemory)
          << " available on device.";
    pause(MinerPauseEnum::PauseDueToInsufficientMemory);
}

void Miner::pause(MinerPauseEnum what) {
    lock_guard<mutex> l(x_pause);
    m_pauseFlags.set(what);
    m_work.header = h256();
    kick_miner();
}

bool Miner::paused() {
    lock_guard<mutex> l(x_pause);
    return m_pauseFlags.any();
}

bool Miner::pauseTest(MinerPauseEnum what) {
    lock_guard<mutex> l(x_pause);
    return m_pauseFlags.test(what);
}

string Miner::pausedString() {
    lock_guard<mutex> l(x_pause);
    string retVar;
    if (m_pauseFlags.any()) {
        for (int i = 0; i < MinerPauseEnum::Pause_MAX; i++) {
            if (m_pauseFlags[(MinerPauseEnum)i]) {
                if (!retVar.empty())
                    retVar.append("; ");

                if (i == MinerPauseEnum::PauseDueToOverHeating)
                    retVar.append("Overheating");
                else if (i == MinerPauseEnum::PauseDueToAPIRequest)
                    retVar.append("Api request");
                else if (i == MinerPauseEnum::PauseDueToFarmPaused)
                    retVar.append("Farm suspended");
                else if (i == MinerPauseEnum::PauseDueToInsufficientMemory)
                    retVar.append("Insufficient GPU memory");
                else if (i == MinerPauseEnum::PauseDueToInitEpochError)
                    retVar.append("Epoch initialization error");
            }
        }
    }
    return retVar;
}

void Miner::resume(MinerPauseEnum fromwhat) {
    lock_guard<mutex> l(x_pause);
    m_pauseFlags.reset(fromwhat);
    // if (!m_pauseFlags.any())
    //{
    //    // TODO Push most recent job from farm ?
    //    // If we do not push a new job the miner will stay idle
    //    // till a new job arrives
    //}
}

float Miner::RetrieveHashRate() noexcept { return m_hashRate.load(memory_order_relaxed); }

void Miner::TriggerHashRateUpdate() noexcept {
    bool b = false;
    if (m_hashRateUpdate.compare_exchange_weak(b, true))
        return;
    // GPU didn't respond to last trigger, assume it's dead.
    // This can happen on CUDA if:
    //   runtime of --cuda-grid-size * --cuda-streams exceeds time of m_collectInterval
    m_hashRate = 0.0;
}

WorkPackage Miner::work() const {
    unique_lock<mutex> l(miner_work_mutex);
    return m_work;
}

void Miner::updateHashRate(uint32_t _groupSize, uint32_t _increment) noexcept {
    m_groupCount += _increment * _groupSize;

    bool b = true;
    if (!m_hashRateUpdate.compare_exchange_weak(b, false))
        return;

    using namespace chrono;
    auto t = steady_clock::now();
    auto us = duration_cast<microseconds>(t - m_hashTime).count();

    m_hashTime = t;

    m_hashRate.store(us ? float(m_groupCount * 1e6) / us : 0.0f, memory_order_relaxed);
    m_groupCount = 0;
}

void Miner::setEpoch(WorkPackage const& w) {
    ethash::epoch_context ec = ethash::get_global_epoch_context(w.epoch);
    m_epochContext.epochNumber = w.epoch;
    m_epochContext.lightNumItems = ec.light_cache_num_items;
    m_epochContext.lightSize = ethash::get_light_cache_size(ec.light_cache_num_items);
    m_epochContext.dagNumItems = ec.full_dataset_num_items;
    m_epochContext.dagSize = ethash::get_full_dataset_size(ec.full_dataset_num_items);
    m_epochContext.lightCache = new ethash_hash512[m_epochContext.lightNumItems];
    memcpy(m_epochContext.lightCache, ec.light_cache, m_epochContext.lightSize);
}

void Miner::freeCache() {
    if (m_epochContext.lightCache) {
        delete[] m_epochContext.lightCache;
        m_epochContext.lightCache = nullptr;
    }
}

} // namespace eth
} // namespace dev
