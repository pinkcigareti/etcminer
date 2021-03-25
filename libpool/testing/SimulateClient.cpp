
/* Copyright (C) 1883 Thomas Edison - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the GPLv3 license, which unfortunately won't be
 * written for another century.
 *
 * You should have received a copy of the LICENSE file with
 * this file.
 */

#include <chrono>
#include <libdev/Log.h>

#include "SimulateClient.h"

using namespace std;
using namespace chrono;
using namespace dev;
using namespace eth;

SimulateClient::SimulateClient(unsigned const& block) : PoolClient(), Worker("sim") { m_block = block; }

SimulateClient::~SimulateClient() = default;

void SimulateClient::connect() {
    // Initialize new session
    m_connected.store(true, memory_order_relaxed);
    m_session = unique_ptr<Session>(new Session);
    m_session->subscribed.store(true, memory_order_relaxed);
    m_session->authorized.store(true, memory_order_relaxed);

    if (m_onConnected)
        m_onConnected();

    // No need to worry about starting again.
    // Worker class prevents that
    startWorking();
}

void SimulateClient::disconnect() {
    cnote << "Simulation results : " << EthWhiteBold << "Max "
          << dev::getFormattedHashes((double)hr_max, ScaleSuffix::Add, 6) << " Mean "
          << dev::getFormattedHashes((double)hr_mean, ScaleSuffix::Add, 6) << EthReset;

    m_conn->addDuration(m_session->duration());
    m_session = nullptr;
    m_connected.store(false, memory_order_relaxed);

    if (m_onDisconnected)
        m_onDisconnected();
}

void SimulateClient::submitHashrate(uint64_t const& rate, string const& id) {
    (void)rate;
    (void)id;
}

void SimulateClient::submitSolution(const Solution& solution) {
    // This is a fake submission only evaluated locally
    chrono::steady_clock::time_point submit_start = chrono::steady_clock::now();
    bool accepted =
        EthashAux::eval(solution.work.epoch, solution.work.header, solution.nonce).value <= solution.work.boundary;
    chrono::milliseconds response_delay_ms =
        chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now() - submit_start);

    if (accepted) {
        if (m_onSolutionAccepted)
            m_onSolutionAccepted(response_delay_ms, solution.midx, false);
    } else {
        if (m_onSolutionRejected)
            m_onSolutionRejected(response_delay_ms, solution.midx);
    }
}

// Handles all logic here
void SimulateClient::workLoop() {
    m_start_time = chrono::steady_clock::now();

    WorkPackage current;
    current.seed = h256::random(); // We don't actually need a real seed as the epoch
                                   // is calculated upon block number (see poolmanager)
    current.header = h256::random();
    current.block = m_block;
    current.boundary = h256(dev::getTargetFromDiff(1));
    m_onWorkReceived(current); // submit new fake job

    while (m_session) {
        float hr = Farm::f().HashRate();
        hr_max = max(hr_max, hr);
        hr_mean = hr_alpha * hr_mean + (1.0f - hr_alpha) * hr;

        this_thread::sleep_for(chrono::milliseconds(200));
    }
}
