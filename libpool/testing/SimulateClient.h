/* Copyright (C) 1883 Thomas Edison - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the GPLv3 license, which unfortunately won't be
 * written for another century.
 *
 * You should have received a copy of the LICENSE file with
 * this file.
 */

#pragma once

#include <iostream>

#include <libdev/Worker.h>
#include <libeth/EthashAux.h>
#include <libeth/Farm.h>
#include <libeth/Miner.h>

#include "../PoolClient.h"

using namespace std;
using namespace dev;
using namespace eth;

class SimulateClient : public PoolClient, Worker {
  public:
    SimulateClient(unsigned const& block);
    ~SimulateClient() override;

    void connect() override;
    void disconnect() override;
    bool isPendingState() override { return false; }
    string ActiveEndPoint() override { return ""; };
    void submitHashrate(uint64_t const& rate, string const& id) override;
    void submitSolution(const Solution& solution) override;

  private:
    void workLoop() override;

    unsigned m_block;
    std::chrono::steady_clock::time_point m_start_time;
    float hr_alpha = 0.45f;
    float hr_max = 0.0f;
    float hr_mean = 0.0f;
};
