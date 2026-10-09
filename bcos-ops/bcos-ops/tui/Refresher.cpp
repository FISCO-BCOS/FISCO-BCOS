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
 * @file Refresher.cpp
 */
#include "Refresher.h"
#include <chrono>

namespace bcos::ops::tui
{
namespace
{
int64_t nowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch())
        .count();
}
}  // namespace

Refresher::Refresher(Connection _connection, LocalFallbacks _fallbacks, Thresholds _thresholds,
    Model& _model, std::function<void()> _onUpdate, int _intervalMs)
  : m_connection(std::move(_connection)),
    m_fallbacks(_fallbacks),
    m_thresholds(_thresholds),
    m_model(_model),
    m_onUpdate(std::move(_onUpdate)),
    m_intervalMs(_intervalMs)
{}

Refresher::~Refresher()
{
    stop();
}

void Refresher::start()
{
    if (m_thread.joinable())
    {
        return;
    }
    m_stop = false;
    m_thread = std::thread([this]() { loop(); });
}

void Refresher::stop()
{
    m_stop = true;
    {
        std::lock_guard<std::mutex> lock(m_wakeMutex);
        m_kick = true;
    }
    m_wake.notify_all();
    if (m_thread.joinable())
    {
        m_thread.join();
    }
}

void Refresher::refreshNow()
{
    {
        std::lock_guard<std::mutex> lock(m_wakeMutex);
        m_kick = true;
    }
    m_wake.notify_all();
}

void Refresher::runOnce()
{
    try
    {
        auto status =
            collectFromRpc(m_connection.call, m_connection.group, m_connection.source, m_fallbacks);
        auto checks = evaluate(status, m_thresholds, nowMs());
        Json::Value params(Json::arrayValue);
        params.append(m_connection.group);
        params.append("");
        Json::Value consensus;
        Json::Value sync;
        Json::Value peers;
        try
        {
            consensus = m_connection.call("getConsensusStatus", params);
        }
        catch (std::exception const&)
        {}
        try
        {
            sync = m_connection.call("getSyncStatus", params);
        }
        catch (std::exception const&)
        {}
        try
        {
            peers = m_connection.call("getPeers", Json::Value(Json::arrayValue));
        }
        catch (std::exception const&)
        {}
        std::lock_guard<std::mutex> lock(m_model.mutex);
        m_model.status = std::move(status);
        m_model.checks = std::move(checks);
        m_model.consensus = std::move(consensus);
        m_model.sync = std::move(sync);
        m_model.peers = std::move(peers);
        m_model.source = m_connection.source;
        m_model.error.clear();
        m_model.lastRefreshMs = nowMs();
    }
    catch (std::exception const& e)
    {
        std::lock_guard<std::mutex> lock(m_model.mutex);
        m_model.error = e.what();
        m_model.lastRefreshMs = nowMs();
    }
    if (m_onUpdate)
    {
        m_onUpdate();
    }
}

void Refresher::loop()
{
    while (!m_stop)
    {
        runOnce();
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(m_intervalMs);
        while (!m_stop)
        {
            std::unique_lock<std::mutex> lock(m_wakeMutex);
            if (m_kick)
            {
                m_kick = false;
                break;
            }
            auto remaining = deadline - std::chrono::steady_clock::now();
            if (remaining <= std::chrono::milliseconds(0))
            {
                break;
            }
            {
                std::lock_guard<std::mutex> modelLock(m_model.mutex);
                m_model.refreshInSeconds = static_cast<int>(
                    std::chrono::duration_cast<std::chrono::seconds>(remaining).count() + 1);
            }
            m_wake.wait_for(lock,
                std::min(remaining, std::chrono::nanoseconds(std::chrono::milliseconds(500))));
        }
    }
}
}  // namespace bcos::ops::tui
