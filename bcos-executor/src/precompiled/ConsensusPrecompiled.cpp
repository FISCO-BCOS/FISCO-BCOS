/**
 *  Copyright (C) 2021 FISCO BCOS.
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
 * @file ConsensusPrecompiled.cpp
 * @author: kyonRay
 * @date 2021-05-26
 */

#include "ConsensusPrecompiled.h"
#include "bcos-executor/src/precompiled/common/PrecompiledResult.h"
#include "bcos-executor/src/precompiled/common/Utilities.h"
#include <bcos-framework//ledger/LedgerTypeDef.h>
#include <bcos-framework//protocol/CommonError.h>
#include <bcos-framework//protocol/Protocol.h>
#include <boost/algorithm/string.hpp>
#include <boost/archive/basic_archive.hpp>
#include <boost/lexical_cast.hpp>
#include <utility>

using namespace bcos;
using namespace bcos::executor;
using namespace bcos::storage;
using namespace bcos::precompiled;
using namespace bcos::ledger;

const char* const CSS_METHOD_ADD_SEALER = "addSealer(string,uint256)";
const char* const CSS_METHOD_ADD_SER = "addObserver(string)";
const char* const CSS_METHOD_REMOVE = "remove(string)";
const char* const CSS_METHOD_SET_WEIGHT = "setWeight(string,uint256)";
const auto NODE_LENGTH = 128u;

ConsensusPrecompiled::ConsensusPrecompiled(crypto::Hash::Ptr _hashImpl) : Precompiled(_hashImpl)
{
    name2Selector[CSS_METHOD_ADD_SEALER] = getFuncSelector(CSS_METHOD_ADD_SEALER, _hashImpl);
    name2Selector[CSS_METHOD_ADD_SER] = getFuncSelector(CSS_METHOD_ADD_SER, _hashImpl);
    name2Selector[CSS_METHOD_REMOVE] = getFuncSelector(CSS_METHOD_REMOVE, _hashImpl);
    name2Selector[CSS_METHOD_SET_WEIGHT] = getFuncSelector(CSS_METHOD_SET_WEIGHT, _hashImpl);
}

std::shared_ptr<PrecompiledExecResult> ConsensusPrecompiled::call(
    std::shared_ptr<executor::TransactionExecutive> _executive,
    PrecompiledExecResult::Ptr _callParameters)
{
    // parse function name
    uint32_t func = getParamFunc(_callParameters->input());
    bytesConstRef data = _callParameters->params();

    showConsensusTable(_executive);

    auto blockContext = _executive->blockContext().lock();
    auto codec = CodecWrapper(blockContext->hashHandler(), blockContext->isWasm());

    if (blockContext->isAuthCheck() && !checkSenderFromAuth(_callParameters->m_sender))
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("ConsensusPrecompiled")
                               << LOG_DESC("sender is not from sys")
                               << LOG_KV("sender", _callParameters->m_sender);
        _callParameters->setExecResult(codec.encode(int32_t(CODE_NO_AUTHORIZED)));
        return _callParameters;
    }

    int result = 0;
    if (func == name2Selector[CSS_METHOD_ADD_SEALER])
    {
        // addSealer(string, uint256)
        result = addSealer(_executive, data, codec);
    }
    else if (func == name2Selector[CSS_METHOD_ADD_SER])
    {
        // addObserver(string)
        result = addObserver(_executive, data, codec);
    }
    else if (func == name2Selector[CSS_METHOD_REMOVE])
    {
        // remove(string)
        result = removeNode(_executive, data, codec);
    }
    else if (func == name2Selector[CSS_METHOD_SET_WEIGHT])
    {
        // setWeight(string,uint256)
        result = setWeight(_executive, data, codec);
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("ConsensusPrecompiled")
                               << LOG_DESC("call undefined function") << LOG_KV("func", func);
        BOOST_THROW_EXCEPTION(
            bcos::protocol::PrecompiledError("ConsensusPrecompiled call undefined function!"));
    }

    _callParameters->setExecResult(codec.encode(int32_t(result)));
    return _callParameters;
}

int ConsensusPrecompiled::addSealer(
    const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& _data,
    const CodecWrapper& codec)
{
    // addSealer(string, uint256)
    std::string nodeID;
    u256 weight;
    auto blockContext = _executive->blockContext().lock();
    codec.decode(_data, nodeID, weight);
    // Uniform lowercase nodeID
    boost::to_lower(nodeID);

    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ConsensusPrecompiled") << LOG_DESC("addSealer func")
                           << LOG_KV("nodeID", nodeID);
    if (nodeID.size() != NODE_LENGTH ||
        std::count_if(nodeID.begin(), nodeID.end(),
            [](unsigned char c) { return std::isxdigit(c); }) != NODE_LENGTH)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("ConsensusPrecompiled")
                               << LOG_DESC("nodeID length error") << LOG_KV("nodeID", nodeID);
        return CODE_INVALID_NODE_ID;
    }
    if (weight == 0)
    {
        // u256 weight be then 0
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("ConsensusPrecompiled") << LOG_DESC("weight is 0")
                               << LOG_KV("nodeID", nodeID);
        return CODE_INVALID_WEIGHT;
    }

    auto& storage = _executive->storage();

    ConsensusNodeList consensusList;
    auto entry = storage.getRow(SYS_CONSENSUS, "key");
    if (entry)
    {
        consensusList = entry->getObject<ConsensusNodeList>();
    }
    else
    {
        entry.emplace(Entry());
    }

    auto it = std::find_if(consensusList.begin(), consensusList.end(),
        [&nodeID](const ConsensusNode& node) { return node.nodeID == nodeID; });
    if (it != consensusList.end())
    {
        it->weight = weight;
        it->type = ledger::CONSENSUS_SEALER;
        it->enableNumber = boost::lexical_cast<std::string>(blockContext->number() + 1);
    }
    else
    {
        consensusList.emplace_back(nodeID, weight, ledger::CONSENSUS_SEALER,
            boost::lexical_cast<std::string>(blockContext->number() + 1));
    }

    entry->setObject(consensusList);

    storage.setRow(SYS_CONSENSUS, "key", std::move(*entry));

    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ConsensusPrecompiled")
                           << LOG_DESC("addSealer successfully insert") << LOG_KV("nodeID", nodeID)
                           << LOG_KV("weight", weight);
    return 0;
}

int ConsensusPrecompiled::addObserver(
    const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& _data,
    const CodecWrapper& codec)
{
    // addObserver(string)
    std::string nodeID;
    auto blockContext = _executive->blockContext().lock();
    codec.decode(_data, nodeID);
    // Uniform lowercase nodeID
    boost::to_lower(nodeID);
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ConsensusPrecompiled") << LOG_DESC("addObserver func")
                           << LOG_KV("nodeID", nodeID);

    if (nodeID.size() != NODE_LENGTH ||
        std::count_if(nodeID.begin(), nodeID.end(),
            [](unsigned char c) { return std::isxdigit(c); }) != NODE_LENGTH)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("ConsensusPrecompiled")
                               << LOG_DESC("nodeID length error") << LOG_KV("nodeID", nodeID);
        return CODE_INVALID_NODE_ID;
    }

    auto& storage = _executive->storage();

    auto entry = storage.getRow(SYS_CONSENSUS, "key");

    ConsensusNodeList consensusList;
    if (entry)
    {
        consensusList = entry->getObject<ConsensusNodeList>();
    }
    else
    {
        entry.emplace(Entry());
    }
    auto it = std::find_if(consensusList.begin(), consensusList.end(),
        [&nodeID](const ConsensusNode& node) { return node.nodeID == nodeID; });
    if (it != consensusList.end())
    {
        it->weight = 0;
        it->type = ledger::CONSENSUS_OBSERVER;
        it->enableNumber = boost::lexical_cast<std::string>(blockContext->number() + 1);
    }
    else
    {
        consensusList.emplace_back(nodeID, 0, ledger::CONSENSUS_OBSERVER,
            boost::lexical_cast<std::string>(blockContext->number() + 1));
    }

    auto sealerCount = std::count_if(consensusList.begin(), consensusList.end(),
        [](auto&& node) { return node.type == ledger::CONSENSUS_SEALER; });

    if (sealerCount == 0)
    {
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ConsensusPrecompiled")
                               << LOG_DESC("addObserver failed, because last sealer");
        return CODE_LAST_SEALER;
    }

    entry->setObject(consensusList);

    storage.setRow(SYS_CONSENSUS, "key", std::move(*entry));

    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ConsensusPrecompiled")
                           << LOG_DESC("addObserver successfully insert");
    return 0;
}

int ConsensusPrecompiled::removeNode(
    const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& _data,
    const CodecWrapper& codec)
{
    // remove(string)
    std::string nodeID;
    auto blockContext = _executive->blockContext().lock();
    codec.decode(_data, nodeID);
    // Uniform lowercase nodeID
    boost::to_lower(nodeID);
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ConsensusPrecompiled") << LOG_DESC("remove func")
                           << LOG_KV("nodeID", nodeID);
    if (nodeID.size() != NODE_LENGTH)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("ConsensusPrecompiled")
                               << LOG_DESC("nodeID length error") << LOG_KV("nodeID", nodeID);
        return CODE_INVALID_NODE_ID;
    }

    auto& storage = _executive->storage();

    ConsensusNodeList consensusList;
    auto entry = storage.getRow(SYS_CONSENSUS, "key");
    if (entry)
    {
        consensusList = entry->getObject<ConsensusNodeList>();
    }
    else
    {
        entry.emplace(Entry());
    }
    auto it = std::find_if(consensusList.begin(), consensusList.end(),
        [&nodeID](const ConsensusNode& node) { return node.nodeID == nodeID; });
    if (it != consensusList.end())
    {
        consensusList.erase(it);
    }
    else
    {
        return CODE_NODE_NOT_EXIST;  // Not found
    }

    auto sealerSize = std::count_if(consensusList.begin(), consensusList.end(),
        [](auto&& node) { return node.type == ledger::CONSENSUS_SEALER; });
    if (sealerSize == 0)
    {
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ConsensusPrecompiled")
                               << LOG_DESC("removeNode failed, because last sealer");
        return CODE_LAST_SEALER;
    }

    entry->setObject(consensusList);

    storage.setRow(SYS_CONSENSUS, "key", std::move(*entry));

    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ConsensusPrecompiled") << LOG_DESC("remove successfully");
    return 0;
}

int ConsensusPrecompiled::setWeight(
    const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& _data,
    const CodecWrapper& codec)
{
    // setWeight(string,uint256)
    std::string nodeID;
    u256 weight;
    auto blockContext = _executive->blockContext().lock();
    codec.decode(_data, nodeID, weight);
    // Uniform lowercase nodeID
    boost::to_lower(nodeID);
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ConsensusPrecompiled") << LOG_DESC("setWeight func")
                           << LOG_KV("nodeID", nodeID);
    if (nodeID.size() != NODE_LENGTH)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("ConsensusPrecompiled")
                               << LOG_DESC("nodeID length error") << LOG_KV("nodeID", nodeID);
        return CODE_INVALID_NODE_ID;
    }
    if (weight == 0)
    {
        // u256 weight must greater then 0
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("ConsensusPrecompiled") << LOG_DESC("weight is 0")
                               << LOG_KV("nodeID", nodeID);
        return CODE_INVALID_WEIGHT;
    }

    auto& storage = _executive->storage();

    auto entry = storage.getRow(SYS_CONSENSUS, "key");

    ConsensusNodeList consensusList;
    if (entry)
    {
        auto value = entry->getField(0);

        consensusList = decodeConsensusList(value);
    }
    auto it = std::find_if(consensusList.begin(), consensusList.end(),
        [&nodeID](const ConsensusNode& node) { return node.nodeID == nodeID; });
    if (it != consensusList.end())
    {
        if (it->type != ledger::CONSENSUS_SEALER)
        {
            BOOST_THROW_EXCEPTION(protocol::PrecompiledError("Cannot set weight to observer."));
        }
        it->weight = weight;
        it->enableNumber = boost::lexical_cast<std::string>(blockContext->number() + 1);
    }
    else
    {
        return CODE_NODE_NOT_EXIST;  // Not found
    }

    entry->setObject(consensusList);

    storage.setRow(SYS_CONSENSUS, "key", std::move(*entry));

    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ConsensusPrecompiled")
                           << LOG_DESC("setWeight successfully");
    return 0;
}

void ConsensusPrecompiled::showConsensusTable(
    const std::shared_ptr<executor::TransactionExecutive>& _executive)
{
    auto& storage = _executive->storage();
    // SYS_CONSENSUS must exist
    auto entry = storage.getRow(SYS_CONSENSUS, "key");

    if (!entry)
    {
        PRECOMPILED_LOG(TRACE) << LOG_BADGE("ConsensusPrecompiled")
                               << LOG_DESC("showConsensusTable") << " No consensus";
        return;
    }

    if (c_fileLogLevel < bcos::LogLevel::TRACE)
    {
        return;
    }
    auto consensusList = entry->getObject<ConsensusNodeList>();

    std::stringstream s;
    s << "ConsensusPrecompiled show table:\n";
    for (auto& it : consensusList)
    {
        auto& [nodeID, weight, type, enableNumber] = it;

        s << "ConsensusPrecompiled: " << nodeID << "," << type << "," << enableNumber << ","
          << weight << "\n";
    }
    PRECOMPILED_LOG(TRACE) << LOG_BADGE("ConsensusPrecompiled") << LOG_DESC("showConsensusTable")
                           << LOG_KV("consensusTable", s.str());
}