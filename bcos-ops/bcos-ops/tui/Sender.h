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
 * @brief the send page's worker: one thread owns the signing key and runs every request the
 *        page submits (load a key, a preset write or read, the HelloWorld deploy, a custom call,
 *        a raw transaction, feature enabling) through the same code the CLI uses, then publishes
 *        a one-line outcome into the Model. The view never encodes or sends anything.
 * @file Sender.h
 */
#pragma once

#include "Model.h"
#include "bcos-ops/RpcCall.h"
#include "bcos-ops/cmd/TxContext.h"
#include "bcos-ops/tx/Presets.h"
#include "bcos-ops/tx/TxSender.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace bcos::ops::tui
{
struct SendRequest
{
    enum class Kind
    {
        loadKey,         // keyFile
        preset,          // preset (+ to override, params, value)
        helloDeploy,     // the embedded HelloWorld
        custom,          // to, signature, params, value
        raw,             // raw: hex or @file
        enableFeatures,  // features
        readFeatures,    // refresh Model.send.features
    };
    Kind kind = Kind::preset;
    std::string keyFile;
    Preset const* preset = nullptr;
    std::string to;
    std::string signature;
    std::vector<std::string> params;
    std::string value;  // decimal wei; empty = none
    std::string raw;
    std::vector<std::string> features;
    bool openTxDetail = true;  // open the produced tx in a detail page (gov votes stay put)
    bool needsKey() const;
};

class Sender
{
public:
    Sender(Connection _connection, Model& _model, std::function<void()> _onUpdate);
    ~Sender();
    void start();
    /// stops after the request in flight (an RPC waits at most its timeout) and joins
    void stop();
    void submit(SendRequest _request);
    /// runs one request on the calling thread and publishes its outcome; the worker calls it,
    /// tests call it directly
    void execute(SendRequest const& _request);
    bool hasKey() const { return m_hasKey.load(); }
    /// the loaded key's address ("" when none)
    std::string keyAddress() const;

private:
    void loop();
    SendOutcome run(SendRequest const& _request);
    ChainFacts const& facts();
    TxSender& sender(bool _needsKey);
    bool proposals();
    void publish(SendOutcome _outcome);

    Connection m_connection;
    Model& m_model;
    std::function<void()> m_onUpdate;
    std::thread m_thread;
    std::mutex m_queueMutex;
    std::condition_variable m_wake;
    std::deque<SendRequest> m_queue;
    std::atomic<bool> m_stop{false};
    std::atomic<bool> m_hasKey{false};
    std::optional<ChainFacts> m_facts;
    std::unique_ptr<TxSender> m_sender;        // the loaded key
    std::unique_ptr<TxSender> m_readerSender;  // a random key for reads
    std::optional<bool> m_proposals;
    std::string m_keyAddress;
    uint64_t m_seq = 0;
};

/// the line the page shows for a preset result: `tx <hash> → block N ok` / the code name /
/// `proposal <id> <status>`
std::string sendOutcomeLine(PresetResult const& _result);
}  // namespace bcos::ops::tui
