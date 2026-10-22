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
 * @brief one function per thread (线索): events in → rows out. Pure; the commands only render.
 * @file Traces.h
 */
#pragma once

#include "LogParser.h"
#include <json/json.h>
#include <string>
#include <vector>

namespace bcos::ops
{
struct TraceResult
{
    std::vector<std::string> header;
    std::vector<std::vector<std::string>> rows;
    std::string missing;  // non-empty when the thread found nothing, with the hint for the user
    Json::Value toJson() const;
};

/// the abridged hash a log line prints ("aa31f96a...") against a full 0x hash
bool hashMatches(std::string const& _logValue, std::string const& _query);
std::string formatTime(int64_t _ms);  // HH:MM:SS.mmm

TraceResult traceViewChange(std::vector<Event> const& _events, size_t _last);
TraceResult tracePbft(std::vector<Event> const& _events, int64_t _number);
TraceResult traceTx(std::vector<Event> const& _events, std::string const& _hash);
TraceResult traceSync(std::vector<Event> const& _events);
TraceResult traceSealStall(std::vector<Event> const& _events);
TraceResult traceP2p(std::vector<Event> const& _events);
TraceResult traceTxSync(std::vector<Event> const& _events);
}  // namespace bcos::ops
