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
 * @file IpTracker.h
 * @brief External-address prediction from peers' Pong `to` statements, after geth's
 *        netutil.IPTracker: one statement per reporter, a sliding time window, and a
 *        minimum number of agreeing reporters before an address is predicted.
 * @date 2026/11/3
 */
#pragma once

#include <bcos-utilities/FixedBytes.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <map>
#include <optional>

namespace bcos::devp2p::disc
{
class IpTracker
{
public:
    using Clock = std::chrono::steady_clock;
    using Address = std::array<uint8_t, 4>;

    // geth: iptrackMinStatements = 10, iptrackWindow = 5 minutes.
    explicit IpTracker(size_t _minStatements = 10,
        Clock::duration _window = std::chrono::minutes(5), size_t _maxStatements = 1024)
      : m_minStatements(_minStatements), m_window(_window), m_maxStatements(_maxStatements)
    {}

    // `_reporter` says it sees us at `_address`. A reporter has one statement: a repeat
    // replaces its previous one (it never counts twice). Statements older than the window
    // are evicted here; beyond maxStatements the oldest statement goes, never the table.
    void addStatement(bcos::h256 const& _reporter, Address const& _address, Clock::time_point _now)
    {
        evictExpired(_now);
        m_statements[_reporter] = Statement{_address, _now};
        if (m_statements.size() > m_maxStatements)
        {
            auto oldest = std::min_element(m_statements.begin(), m_statements.end(),
                [](auto const& _a, auto const& _b) { return _a.second.at < _b.second.at; });
            m_statements.erase(oldest);
        }
    }

    // The address with the most in-window statements, if it has at least minStatements
    // and no other address ties it.
    std::optional<Address> predict(Clock::time_point _now)
    {
        evictExpired(_now);
        std::map<Address, size_t> counts;
        for (auto const& [reporter, statement] : m_statements)
        {
            ++counts[statement.address];
        }
        std::optional<Address> best;
        size_t bestCount = 0;
        bool tie = false;
        for (auto const& [address, count] : counts)
        {
            if (count > bestCount)
            {
                best = address;
                bestCount = count;
                tie = false;
            }
            else if (count == bestCount)
            {
                tie = true;
            }
        }
        if (!best || tie || bestCount < m_minStatements)
        {
            return std::nullopt;
        }
        return best;
    }

    size_t size() const { return m_statements.size(); }

private:
    struct Statement
    {
        Address address;
        Clock::time_point at;
    };
    void evictExpired(Clock::time_point _now)
    {
        for (auto it = m_statements.begin(); it != m_statements.end();)
        {
            if (_now - it->second.at > m_window)
            {
                it = m_statements.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    size_t m_minStatements;
    Clock::duration m_window;
    size_t m_maxStatements;
    std::map<bcos::h256, Statement> m_statements;
};
}  // namespace bcos::devp2p::disc
