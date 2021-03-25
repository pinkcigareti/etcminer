/* Copyright (C) 1883 Thomas Edison - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the GPLv3 license, which unfortunately won't be
 * written for another century.
 *
 * You should have received a copy of the LICENSE file with
 * this file.
 */

#pragma once

#include <signal.h>

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

extern bool g_exitOnError;

namespace dev {
enum class WorkerState { Starting, Started, Stopping, Stopped, Killing };

class Worker {
  public:
    Worker(std::string _name) : m_name(std::move(_name)) {}

    Worker(Worker const&) = delete;
    Worker& operator=(Worker const&) = delete;

    virtual ~Worker();

    /// Starts worker thread; causes startedWorking() to be called.
    void startWorking();

    /// Triggers worker thread it should stop
    void triggerStopWorking();

    /// Stop worker thread; causes call to stopWorking() and waits till thread has stopped.
    void stopWorking();

    /// Whether or not this worker should stop
    bool shouldStop() const { return m_state != WorkerState::Started; }

  private:
    virtual void workLoop() = 0;

    std::string m_name;
    mutable std::mutex workerWorkMutex;  ///< Lock for the network existence.
    std::unique_ptr<std::thread> m_work; ///< The network thread.
    std::atomic<WorkerState> m_state = {WorkerState::Starting};
};

} // namespace dev
