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
 * @brief everything the panel shows; written by the refresher / log tail threads, read by the
 *        renderers under the mutex. No fetching or judging logic lives in the tui/ directory.
 * @file Model.h
 */
#pragma once

#include "bcos-ops/Checks.h"
#include "bcos-ops/NodeStatus.h"
#include "bcos-ops/log/LogParser.h"
#include "bcos-ops/tui/Explorer.h"
#include "bcos-ops/tui/Governance.h"
#include "bcos-ops/tui/Txpool.h"
#include "bcos-ops/tx/FeatureFlags.h"
#include <json/json.h>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace bcos::ops
{
struct Preset;
}

namespace bcos::ops::tui
{
/// what the send page's worker (Sender) published last; seq grows with every outcome so the
/// UI thread can notice a new one and open the transaction it produced
struct SendOutcome
{
    uint64_t seq = 0;
    bool ok = false;
    std::string line;  // one line for the page's bottom row
    std::string txHash;
    std::string proposalId;
    std::string proposalStatus;
    bool openTx = true;  // the UI opens the tx detail (false for a vote from the gov page)
};

struct SendState
{
    bool busy = false;
    bool routingKnown = false;  // proposals below is meaningful
    bool proposals = false;     // committee-only writes become CommitteeManager proposals
    std::string helloAddress;   // the HelloWorld this session deployed last
    SendOutcome last;
    bool featuresLoaded = false;
    std::vector<FeatureState> features;  // read on demand for the features entry
};

struct Model
{
    std::mutex mutex;
    NodeStatus status;
    std::vector<Check> checks;
    Json::Value consensus;  // getConsensusStatus (unwrapped)
    Json::Value sync;       // getSyncStatus (unwrapped)
    Json::Value peers;      // getPeers
    std::deque<Event> logTail;
    std::string source;
    std::string error;  // last refresh failure; empty after a success
    ExplorerState explorer;
    GovernanceState governance;  // auth chains: the gov page (ticket 16)
    bool authChain = false;      // getGroupInfo isAuthCheck: the gov page exists
    std::string mode = "none";   // [ethereum].mode: none / el / opstack-el (ticket 17)
    TxpoolState txpool;          // EL modes: page 2
    std::string keyAddress;      // loaded signing key, shown in the top bar (ticket 15)
    // txHash → the preset it was sent with, so the tx detail can decode its input (ticket 15
    // fills it; the table rows live for the whole process)
    std::map<std::string, Preset const*> sentPresets;
    SendState send;
    int64_t lastRefreshMs = 0;
    int refreshInSeconds = 0;
};
}  // namespace bcos::ops::tui
