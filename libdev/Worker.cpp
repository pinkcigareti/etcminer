
/* Copyright (C) 1883 Thomas Edison - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the GPLv3 license, which unfortunately won't be
 * written for another century.
 *
 * You should have received a copy of the LICENSE file with
 * this file.
 */

#include <chrono>
#include <thread>

#include "Log.h"
#include "Worker.h"

using namespace std;
using namespace dev;

void Worker::startWorking() {
    //	cnote << "startWorking for thread" << m_name;
    unique_lock<mutex> l(workerWorkMutex);

    if (m_work) {
        WorkerState ex = WorkerState::Stopped;
        m_state.compare_exchange_strong(ex, WorkerState::Starting);
    } else {
        m_state = WorkerState::Starting;
        m_work.reset(new thread([&]() {
            setThreadName(m_name.c_str());
            //			cnote << "Thread begins";
            while (m_state != WorkerState::Killing) {
                WorkerState ex = WorkerState::Starting;
                bool ok = m_state.compare_exchange_strong(ex, WorkerState::Started);
                //				cnote << "Trying to set Started: Thread was" << (unsigned)ex << "; "
                //<< ok;
                (void)ok;

                try {
                    workLoop();
                } catch (exception const& _e) {
                    ccrit << "Exception thrown in Worker thread: " << _e.what();
                    if (g_exitOnError) {
                        ccrit << "Terminating due to --exit";
                        raise(SIGTERM);
                    }
                }

                //				ex = WorkerState::Stopping;
                //				m_state.compare_exchange_strong(ex, WorkerState::Stopped);

                ex = m_state.exchange(WorkerState::Stopped);
                //				cnote << "State: Stopped: Thread was" << (unsigned)ex;
                if (ex == WorkerState::Killing || ex == WorkerState::Starting)
                    m_state.exchange(ex);

                while (m_state == WorkerState::Stopped)
                    this_thread::sleep_for(chrono::milliseconds(20));
            }
        }));
        //		cnote << "Spawning" << m_name;
    }
    while (m_state == WorkerState::Starting)
        this_thread::sleep_for(chrono::microseconds(20));
}

void Worker::triggerStopWorking() {
    unique_lock<mutex> l(workerWorkMutex);
    if (m_work) {
        WorkerState ex = WorkerState::Started;
        m_state.compare_exchange_strong(ex, WorkerState::Stopping);
    }
}

void Worker::stopWorking() {
    unique_lock<mutex> l(workerWorkMutex);
    if (m_work) {
        WorkerState ex = WorkerState::Started;
        m_state.compare_exchange_strong(ex, WorkerState::Stopping);

        while (m_state != WorkerState::Stopped)
            this_thread::sleep_for(chrono::microseconds(20));
    }
}

Worker::~Worker() {
    unique_lock<mutex> l(workerWorkMutex);
    if (m_work) {
        m_state.exchange(WorkerState::Killing);
        m_work->join();
        m_work.reset();
    }
}
