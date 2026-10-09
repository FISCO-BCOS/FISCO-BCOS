/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @brief worker thread: every interval collect status + checks + raw consensus/sync/peers
 * @file Refresher.h
 */
#pragma once

#include "Model.h"
#include "bcos-ops/Checks.h"
#include "bcos-ops/RpcCall.h"
#include "bcos-ops/collect/RpcCollector.h"
#include <atomic>
#include <condition_variable>
#include <functional>
#include <thread>

namespace bcos::ops::tui
{
class Refresher
{
public:
    Refresher(Connection _connection, LocalFallbacks _fallbacks, Thresholds _thresholds,
        Model& _model, std::function<void()> _onUpdate, int _intervalMs = 2000);
    ~Refresher();
    void start();
    void stop();
    void refreshNow();
    /// one collection pass (what the thread does each tick); public for tests
    void runOnce();

private:
    void loop();

    Connection m_connection;
    LocalFallbacks m_fallbacks;
    Thresholds m_thresholds;
    Model& m_model;
    std::function<void()> m_onUpdate;
    int m_intervalMs;
    std::thread m_thread;
    std::mutex m_wakeMutex;
    std::condition_variable m_wake;
    std::atomic<bool> m_stop{false};
    bool m_kick = false;
};
}  // namespace bcos::ops::tui
