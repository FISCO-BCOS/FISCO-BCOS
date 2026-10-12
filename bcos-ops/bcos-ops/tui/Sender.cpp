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
 * @file Sender.cpp
 */
#include "Sender.h"
#include "bcos-ops/OpsError.h"
#include "bcos-ops/Output.h"
#include "bcos-ops/tx/Abi.h"
#include "bcos-ops/tx/Account.h"
#include "bcos-ops/tx/AuthMode.h"
#include "bcos-ops/tx/FeatureFlags.h"
#include "bcos-ops/tx/HelloWorld.h"
#include "bcos-ops/tx/PresetCall.h"
#include <bcos-utilities/DataConvertUtility.h>
#include <fstream>
#include <sstream>

namespace bcos::ops::tui
{
namespace
{
std::string senderReadFile(std::string const& _path)
{
    std::ifstream in(_path);
    if (!in)
    {
        throw OpsError(c_exitUsage, "cannot read " + _path);
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    auto text = buffer.str();
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0)
    {
        text.pop_back();
    }
    return text;
}
}  // namespace

bool SendRequest::needsKey() const
{
    switch (kind)
    {
    case Kind::loadKey:
    case Kind::readFeatures:
        return false;
    case Kind::preset:
        return preset != nullptr && preset->write;
    default:
        return true;
    }
}

std::string sendOutcomeLine(PresetResult const& _result)
{
    std::string line;
    if (_result.proposal)
    {
        line = "proposal " + _result.proposal->id + " " + _result.proposal->statusName;
        if (_result.proposal->executed)
        {
            line += " executed";
        }
        if (_result.proposal->execCode && *_result.proposal->execCode != 0)
        {
            line += " " + std::to_string(*_result.proposal->execCode) + " " +
                    _result.proposal->execCodeName;
        }
        return line;
    }
    if (!_result.receipt.txHash.empty())
    {
        line = "tx " + abridged(_result.receipt.txHash, 6) + " -> block " +
               std::to_string(_result.receipt.blockNumber);
    }
    if (!_result.receipt.ok())
    {
        line += (line.empty() ? "" : " ") + std::string("FAILED status ") +
                std::to_string(_result.receipt.status);
        if (!_result.receipt.message.empty())
        {
            line += " " + sanitizeForTerminal(_result.receipt.message);
        }
        return line;
    }
    if (_result.code)
    {
        return *_result.code == 0 ?
                   line + " ok" :
                   line + " code " + std::to_string(*_result.code) + " " + _result.codeName;
    }
    if (!_result.decoded.isNull())
    {
        Json::StreamWriterBuilder writer;
        writer["indentation"] = "";
        auto decoded = Json::writeString(writer, _result.decoded);
        line += (line.empty() ? "result " : " result ") + sanitizeForTerminal(decoded);
    }
    else if (!line.empty())
    {
        line += " ok";
    }
    return line;
}

Sender::Sender(Connection _connection, Model& _model, std::function<void()> _onUpdate)
  : m_connection(std::move(_connection)), m_model(_model), m_onUpdate(std::move(_onUpdate))
{}

Sender::~Sender()
{
    stop();
}

void Sender::start()
{
    if (m_thread.joinable())
    {
        return;
    }
    m_stop = false;
    m_thread = std::thread([this]() { loop(); });
}

void Sender::stop()
{
    m_stop = true;
    m_wake.notify_all();
    if (m_thread.joinable())
    {
        m_thread.join();
    }
}

void Sender::submit(SendRequest _request)
{
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_queue.push_back(std::move(_request));
    }
    {
        std::lock_guard<std::mutex> lock(m_model.mutex);
        m_model.send.busy = true;
    }
    m_wake.notify_all();
}

std::string Sender::keyAddress() const
{
    return m_keyAddress;
}

void Sender::loop()
{
    while (!m_stop)
    {
        SendRequest request;
        {
            std::unique_lock<std::mutex> lock(m_queueMutex);
            m_wake.wait(lock, [this]() { return m_stop || !m_queue.empty(); });
            if (m_stop)
            {
                return;
            }
            request = std::move(m_queue.front());
            m_queue.pop_front();
        }
        execute(request);
    }
}

ChainFacts const& Sender::facts()
{
    if (!m_facts)
    {
        m_facts = chainFacts(m_connection);
    }
    return *m_facts;
}

TxSender& Sender::sender(bool _needsKey)
{
    if (_needsKey)
    {
        if (!m_sender)
        {
            throw OpsError(c_exitUsage, "load a key first");
        }
        return *m_sender;
    }
    if (m_sender)
    {
        return *m_sender;
    }
    if (!m_readerSender)
    {
        auto const& chain = facts();
        m_readerSender = std::make_unique<TxSender>(m_connection.call, m_connection.group,
            chain.chainId, chain.sm, makeAccount(chain.sm, std::nullopt));
    }
    return *m_readerSender;
}

bool Sender::proposals()
{
    if (!m_proposals)
    {
        m_proposals = chainUsesProposals(m_connection);
        std::lock_guard<std::mutex> lock(m_model.mutex);
        m_model.send.proposals = *m_proposals;
        m_model.send.routingKnown = true;
    }
    return *m_proposals;
}

void Sender::publish(SendOutcome _outcome)
{
    {
        std::lock_guard<std::mutex> lock(m_model.mutex);
        _outcome.seq = ++m_seq;
        m_model.send.last = std::move(_outcome);
        bool more = false;
        {
            std::lock_guard<std::mutex> queueLock(m_queueMutex);
            more = !m_queue.empty();
        }
        m_model.send.busy = more;
    }
    if (m_onUpdate)
    {
        m_onUpdate();
    }
}

void Sender::execute(SendRequest const& _request)
{
    SendOutcome outcome;
    try
    {
        outcome = run(_request);
    }
    catch (std::exception const& e)
    {
        outcome.ok = false;
        outcome.line = std::string("error: ") + sanitizeForTerminal(e.what());
    }
    publish(std::move(outcome));
}

SendOutcome Sender::run(SendRequest const& _request)
{
    SendOutcome outcome;
    using Kind = SendRequest::Kind;
    switch (_request.kind)
    {
    case Kind::loadKey:
    {
        auto const& chain = facts();
        auto keyPair = makeAccount(chain.sm, _request.keyFile);
        auto address = accountAddress(chain.sm, *keyPair);
        m_sender = std::make_unique<TxSender>(
            m_connection.call, m_connection.group, chain.chainId, chain.sm, std::move(keyPair));
        m_keyAddress = address;
        m_hasKey = true;
        {
            std::lock_guard<std::mutex> lock(m_model.mutex);
            m_model.keyAddress = address;
        }
        proposals();  // known before the first confirmation layer
        outcome.ok = true;
        outcome.line = "key loaded: " + address;
        return outcome;
    }
    case Kind::readFeatures:
    {
        auto states = readFeatureStates(m_connection.call, m_connection.group);
        std::lock_guard<std::mutex> lock(m_model.mutex);
        m_model.send.features = std::move(states);
        m_model.send.featuresLoaded = true;
        outcome.ok = true;
        outcome.line = "features read";
        return outcome;
    }
    case Kind::helloDeploy:
    {
        auto& tx = sender(true);
        auto receipt = tx.send("", fromHex(std::string(helloworld::binary(facts().sm))),
            std::string(helloworld::c_abi));
        outcome.txHash = receipt.txHash;
        outcome.ok = receipt.ok();
        if (receipt.ok())
        {
            std::lock_guard<std::mutex> lock(m_model.mutex);
            m_model.send.helloAddress = receipt.contractAddress;
            outcome.line = "HelloWorld deployed at " + receipt.contractAddress + " (tx " +
                           abridged(receipt.txHash, 6) + ", block " +
                           std::to_string(receipt.blockNumber) + ")";
        }
        else
        {
            outcome.line = "deploy FAILED status " + std::to_string(receipt.status) + " " +
                           sanitizeForTerminal(receipt.message);
        }
        return outcome;
    }
    case Kind::preset:
    {
        if (_request.preset == nullptr)
        {
            throw OpsError(c_exitUsage, "no preset selected");
        }
        auto const& preset = *_request.preset;
        auto& tx = sender(preset.write);
        PresetRunner runner(tx, facts().sm, m_connection.call, m_connection.group);
        Routing routing;
        if (preset.proposal)
        {
            routing.proposals = proposals();
        }
        runner.setRouting(routing);
        auto result = runner.run(preset, _request.params, _request.to);
        outcome.ok = result.ok;
        outcome.txHash = result.receipt.txHash;
        if (result.proposal)
        {
            outcome.proposalId = result.proposal->id;
            outcome.proposalStatus = result.proposal->statusName;
        }
        outcome.line = sendOutcomeLine(result);
        if (!outcome.txHash.empty())
        {
            std::lock_guard<std::mutex> lock(m_model.mutex);
            m_model.sentPresets[outcome.txHash] = &preset;
        }
        return outcome;
    }
    case Kind::custom:
    {
        auto& tx = sender(true);
        Abi abi(facts().sm);
        auto data =
            abi.encodeMethodBySignature(_request.signature, argsToJsonArray(_request.params));
        SendOptions options;
        options.value = _request.value;
        auto receipt = tx.send(_request.to, std::move(data), "", options);
        outcome.txHash = receipt.txHash;
        outcome.ok = receipt.ok();
        outcome.line = "tx " + abridged(receipt.txHash, 6) + " -> block " +
                       std::to_string(receipt.blockNumber) +
                       (receipt.ok() ? " ok" : " FAILED status " + std::to_string(receipt.status));
        if (!receipt.message.empty())
        {
            outcome.line += " " + sanitizeForTerminal(receipt.message);
        }
        if (receipt.ok() && receipt.output.size() > 2)
        {
            outcome.line += " output " + abridged(receipt.output, 8);
        }
        return outcome;
    }
    case Kind::raw:
    {
        auto& tx = sender(true);
        auto hex =
            _request.raw.starts_with("@") ? senderReadFile(_request.raw.substr(1)) : _request.raw;
        auto receipt = tx.sendRaw(hex);
        outcome.txHash = receipt.txHash;
        outcome.ok = receipt.ok();
        outcome.line = "raw tx " + abridged(receipt.txHash, 6) + " -> block " +
                       std::to_string(receipt.blockNumber) +
                       (receipt.ok() ? " ok" : " FAILED status " + std::to_string(receipt.status));
        return outcome;
    }
    case Kind::enableFeatures:
    {
        auto& tx = sender(true);
        PresetRunner runner(tx, facts().sm, m_connection.call, m_connection.group);
        Routing routing;
        routing.proposals = proposals();
        runner.setRouting(routing);
        auto steps =
            enableFeatures(m_connection.call, m_connection.group, runner, _request.features);
        size_t enabled = 0;
        size_t skipped = 0;
        std::string failed;
        for (auto const& step : steps)
        {
            if (step.action == "enabled")
            {
                ++enabled;
            }
            else if (step.action == "skipped")
            {
                ++skipped;
            }
            else
            {
                failed =
                    step.name + (step.result ? " " + sendOutcomeLine(*step.result) : std::string());
            }
        }
        outcome.ok = failed.empty();
        outcome.line = std::to_string(enabled) + " enabled, " + std::to_string(skipped) +
                       " already on" + (failed.empty() ? "" : ", failed: " + failed);
        auto states = readFeatureStates(m_connection.call, m_connection.group);
        std::lock_guard<std::mutex> lock(m_model.mutex);
        m_model.send.features = std::move(states);
        m_model.send.featuresLoaded = true;
        return outcome;
    }
    }
    return outcome;
}
}  // namespace bcos::ops::tui
