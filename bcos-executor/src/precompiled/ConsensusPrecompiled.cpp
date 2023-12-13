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
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-framework/protocol/CommonError.h>
#include <bcos-framework/protocol/Protocol.h>
#include <boost/algorithm/string.hpp>
#include <boost/archive/basic_archive.hpp>
#include <boost/lexical_cast.hpp>
#include <utility>

using namespace bcos;
using namespace bcos::executor;
using namespace bcos::storage;
using namespace bcos::precompiled;
using namespace bcos::ledger;

constexpr const char* const CSS_METHOD_ADD_SEALER = "addSealer(string,uint256)";
constexpr const char* const CSS_METHOD_ADD_SER = "addObserver(string)";
constexpr const char* const CSS_METHOD_REMOVE = "remove(string)";
constexpr const char* const CSS_METHOD_SET_WEIGHT = "setWeight(string,uint256)";
const auto NODE_LENGTH = 128U;

ConsensusPrecompiled::ConsensusPrecompiled(const crypto::Hash::Ptr& _hashImpl)
  : Precompiled(_hashImpl)
{
    name2Selector[CSS_METHOD_ADD_SEALER] = getFuncSelector(CSS_METHOD_ADD_SEALER, _hashImpl);
    name2Selector[CSS_METHOD_ADD_SER] = getFuncSelector(CSS_METHOD_ADD_SER, _hashImpl);
    name2Selector[CSS_METHOD_REMOVE] = getFuncSelector(CSS_METHOD_REMOVE, _hashImpl);
    name2Selector[CSS_METHOD_SET_WEIGHT] = getFuncSelector(CSS_METHOD_SET_WEIGHT, _hashImpl);
    name2Selector[WSM_METHOD_ROTATE_STR] = getFuncSelector(WSM_METHOD_ROTATE_STR, _hashImpl);
}

std::shared_ptr<PrecompiledExecResult> ConsensusPrecompiled::call(
    std::shared_ptr<executor::TransactionExecutive> _executive,
    PrecompiledExecResult::Ptr _callParameters)
{
    // parse function name
    uint32_t func = getParamFunc(_callParameters->input());
    bytesConstRef data = _callParameters->params();

    showConsensusTable(_executive);

    const auto& blockContext = _executive->blockContext();
    auto codec = CodecWrapper(blockContext.hashHandler(), blockContext.isWasm());

    if (blockContext.isAuthCheck() && !checkSenderFromAuth(_callParameters->m_sender))
    {
        if (!blockContext.features().get(Features::Flag::feature_rpbft) ||
            func != name2Selector[WSM_METHOD_ROTATE_STR])
        {
            PRECOMPILED_LOG(DEBUG)
                << LOG_BADGE("ConsensusPrecompiled") << LOG_DESC("sender is not from sys")
                << LOG_KV("sender", _callParameters->m_sender);
            _callParameters->setExecResult(codec.encode(int32_t(CODE_NO_AUTHORIZED)));
            return _callParameters;
        }
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
    else if (blockContext.features().get(Features::Flag::feature_rpbft) &&
             func == name2Selector[WSM_METHOD_ROTATE_STR])
    {
        rotateWorkingSealer(_executive, _callParameters, codec);
    }
    else [[unlikely]]
    {
        PRECOMPILED_LOG(INFO) << LOG_BADGE("ConsensusPrecompiled")
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
    const auto& blockContext = _executive->blockContext();
    codec.decode(_data, nodeID, weight);
    // Uniform lowercase nodeID
    boost::to_lower(nodeID);

    PRECOMPILED_LOG(INFO) << BLOCK_NUMBER(blockContext.number())
                          << LOG_BADGE("ConsensusPrecompiled") << LOG_DESC("addSealer")
                          << LOG_KV("nodeID", nodeID);
    if (nodeID.size() != NODE_LENGTH ||
        std::count_if(nodeID.begin(), nodeID.end(),
            [](unsigned char _ch) { return std::isxdigit(_ch); }) != NODE_LENGTH)
    {
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ConsensusPrecompiled")
                               << LOG_DESC("nodeID length mistake") << LOG_KV("nodeID", nodeID);
        return CODE_INVALID_NODE_ID;
    }
    if (weight == 0)
    {
        // u256 weight be then 0
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ConsensusPrecompiled") << LOG_DESC("weight is 0")
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

    auto node = std::find_if(consensusList.begin(), consensusList.end(),
        [&nodeID](const ConsensusNode& node) { return node.nodeID == nodeID; });
    if (node != consensusList.end())
    {
        // exist
        node->weight = weight;
        if (blockContext.features().get(Features::Flag::feature_rpbft))
        {
            node->type = ledger::CONSENSUS_CANDIDATE_SEALER;
        }
        else
        {
            node->type = ledger::CONSENSUS_SEALER;
        }
        node->enableNumber = boost::lexical_cast<std::string>(blockContext.number() + 1);
    }
    else
    {
        // no exist
        if (blockContext.blockVersion() >= (uint32_t)protocol::BlockVersion::V3_1_VERSION)
        {
            // version >= 3.1.0, only allow adding sealer in observer list
            return CODE_ADD_SEALER_SHOULD_IN_OBSERVER;
        }
        consensusList.emplace_back(nodeID, weight, std::string{ledger::CONSENSUS_SEALER},
            boost::lexical_cast<std::string>(blockContext.number() + 1));
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
    const auto& blockContext = _executive->blockContext();
    codec.decode(_data, nodeID);
    // Uniform lowercase nodeID
    boost::to_lower(nodeID);
    PRECOMPILED_LOG(INFO) << BLOCK_NUMBER(blockContext.number())
                          << LOG_BADGE("ConsensusPrecompiled") << LOG_DESC("addObserver")
                          << LOG_KV("nodeID", nodeID);

    if (nodeID.size() != NODE_LENGTH ||
        std::count_if(nodeID.begin(), nodeID.end(),
            [](unsigned char c) { return std::isxdigit(c); }) != NODE_LENGTH)
    {
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ConsensusPrecompiled")
                               << LOG_DESC("nodeID length mistake") << LOG_KV("nodeID", nodeID);
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
    auto node = std::find_if(consensusList.begin(), consensusList.end(),
        [&nodeID](const ConsensusNode& node) { return node.nodeID == nodeID; });
    if (node != consensusList.end())
    {
        // find it in consensus list
        auto sealerCount = std::count_if(consensusList.begin(), consensusList.end(),
            [](auto&& node) { return node.type == ledger::CONSENSUS_SEALER; });
        if (sealerCount == 1)
        {
            PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ConsensusPrecompiled")
                                   << LOG_DESC("addObserver failed, because last sealer");
            return CODE_LAST_SEALER;
        }
        node->weight = 0;
        node->type = ledger::CONSENSUS_OBSERVER;
        node->enableNumber = boost::lexical_cast<std::string>(blockContext.number() + 1);
    }
    else
    {
        consensusList.emplace_back(nodeID, 0, std::string{ledger::CONSENSUS_OBSERVER},
            boost::lexical_cast<std::string>(blockContext.number() + 1));
    }

    entry->setObject(consensusList);

    storage.setRow(SYS_CONSENSUS, "key", std::move(*entry));

    return 0;
}

int ConsensusPrecompiled::removeNode(
    const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& _data,
    const CodecWrapper& codec)
{
    // remove(string)
    std::string nodeID;
    const auto& blockContext = _executive->blockContext();
    codec.decode(_data, nodeID);
    // Uniform lowercase nodeID
    boost::to_lower(nodeID);
    PRECOMPILED_LOG(INFO) << LOG_BADGE("ConsensusPrecompiled") << LOG_DESC("remove")
                          << LOG_KV("nodeID", nodeID);
    if (nodeID.size() != NODE_LENGTH)
    {
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ConsensusPrecompiled")
                               << LOG_DESC("nodeID length mistake") << LOG_KV("nodeID", nodeID);
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
    auto node = std::find_if(consensusList.begin(), consensusList.end(),
        [&nodeID](const ConsensusNode& node) { return node.nodeID == nodeID; });
    if (node != consensusList.end())
    {
        consensusList.erase(node);
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

    return 0;
}

int ConsensusPrecompiled::setWeight(
    const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& _data,
    const CodecWrapper& codec)
{
    // setWeight(string,uint256)
    std::string nodeID;
    u256 weight;
    const auto& blockContext = _executive->blockContext();
    codec.decode(_data, nodeID, weight);
    // Uniform lowercase nodeID
    boost::to_lower(nodeID);
    PRECOMPILED_LOG(INFO) << LOG_BADGE("ConsensusPrecompiled") << LOG_DESC("setWeight")
                          << LOG_KV("nodeID", nodeID) << LOG_KV("weight", weight);
    if (nodeID.size() != NODE_LENGTH)
    {
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ConsensusPrecompiled")
                               << LOG_DESC("nodeID length mistake") << LOG_KV("nodeID", nodeID);
        return CODE_INVALID_NODE_ID;
    }
    if (weight == 0)
    {
        // u256 weight must greater then 0
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ConsensusPrecompiled") << LOG_DESC("weight is 0")
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
    auto node = std::find_if(consensusList.begin(), consensusList.end(),
        [&nodeID](const ConsensusNode& node) { return node.nodeID == nodeID; });
    if (node != consensusList.end())
    {
        if (node->type == ledger::CONSENSUS_OBSERVER)
        {
            BOOST_THROW_EXCEPTION(protocol::PrecompiledError("Cannot set weight to observer."));
        }
        node->weight = weight;
        node->enableNumber = boost::lexical_cast<std::string>(blockContext.number() + 1);
    }
    else
    {
        return CODE_NODE_NOT_EXIST;  // Not found
    }

    entry->setObject(consensusList);

    storage.setRow(SYS_CONSENSUS, "key", std::move(*entry));

    return 0;
}

void ConsensusPrecompiled::rotateWorkingSealer(
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    const PrecompiledExecResult::Ptr& _callParameters, const CodecWrapper& codec)
{
    auto const& blockContext = _executive->blockContext();
    PRECOMPILED_LOG(INFO) << BLOCK_NUMBER(blockContext.number()) << LOG_DESC("rotateWorkingSealer");
    bytes vrfPublicKey;
    bytes vrfInput;
    bytes vrfProof;
    codec.decode(_callParameters->params(), vrfPublicKey, vrfInput, vrfProof);
    try
    {
        WorkingSealerManagerImpl sealerManger;
        sealerManger.createVRFInfo(
            std::move(vrfProof), std::move(vrfPublicKey), std::move(vrfInput));
        sealerManger.rotateWorkingSealer(_executive, _callParameters);
    }
    catch (protocol::PrecompiledError const& _e)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("WorkingSealerManagerPrecompiled")
                               << LOG_DESC("rotateWorkingSealer exception occurred")
                               << LOG_KV("msg", _e.what())
                               << LOG_KV("origin", _callParameters->m_origin)
                               << LOG_KV("sender", _callParameters->m_sender);
        BOOST_THROW_EXCEPTION(_e);
    }
    catch (std::exception const& _e)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("WorkingSealerManagerPrecompiled")
                               << LOG_DESC("rotateWorkingSealer exception occurred")
                               << LOG_KV("msg", boost::diagnostic_information(_e))
                               << LOG_KV("origin", _callParameters->m_origin)
                               << LOG_KV("sender", _callParameters->m_sender);
        BOOST_THROW_EXCEPTION(
            protocol::PrecompiledError("RotateWorkingSealer exception occurred."));
    }
}

void ConsensusPrecompiled::showConsensusTable(
    const std::shared_ptr<executor::TransactionExecutive>& _executive)
{
    if (c_fileLogLevel < bcos::LogLevel::TRACE)
    {
        return;
    }
    auto& storage = _executive->storage();
    // SYS_CONSENSUS must exist
    auto entry = storage.getRow(SYS_CONSENSUS, "key");

    if (!entry)
    {
        PRECOMPILED_LOG(TRACE) << LOG_BADGE("ConsensusPrecompiled")
                               << LOG_DESC("showConsensusTable") << " No consensus";
        return;
    }

    auto consensusList = entry->getObject<ConsensusNodeList>();

    std::stringstream consensusTable;
    consensusTable << "ConsensusPrecompiled show table:\n";
    for (auto& node : consensusList)
    {
        auto& [nodeID, weight, type, enableNumber] = node;

        consensusTable << "ConsensusPrecompiled: " << nodeID << "," << type << "," << enableNumber
                       << "," << weight << "\n";
    }
    PRECOMPILED_LOG(TRACE) << LOG_BADGE("ConsensusPrecompiled") << LOG_DESC("showConsensusTable")
                           << LOG_KV("consensusTable", consensusTable.str());
}