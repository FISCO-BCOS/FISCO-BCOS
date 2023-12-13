/*
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
 * @file JsonRpcServiceImpl.cpp
 * @author: octopus
 * @date 2023-02-22
 */

#include <bcos-cpp-sdk/rpc/JsonRpcServiceImpl.h>
#include <bcos-utilities/BoostLog.h>
#include <bcos-utilities/Error.h>

using namespace bcos;
using namespace bcos::cppsdk;
using namespace bcos::cppsdk::jsonrpc;

std::string JsonRpcServiceImpl::sendTransaction(const bcos::crypto::KeyPairInterface& _keyPair,
    const std::string& _groupID, const std::string& _nodeName, const std::string& _to,
    bcos::bytes&& _data, std::string _abi, int32_t _attribute, std::string _extraData,
    RespFunc _respFunc)
{
    auto service = m_rpc->service();
    auto groupInfo = service->getGroupInfo(_groupID);
    if (!groupInfo)
    {
        BCOS_LOG(TRACE) << LOG_BADGE("JsonRpcServiceImpl::sendTransaction")
                        << LOG_DESC("group not exist") << LOG_KV("groupID", _groupID);
        auto error = BCOS_ERROR_PTR(-1, "group not exist, groupID: " + _groupID);
        _respFunc(std::move(error), nullptr);
        return std::string("");
    }

    int64_t _blockLimit = 0;
    service->getBlockLimit(_groupID, _blockLimit);
    std::string chainID = groupInfo->chainID();

    auto result = m_transactionBuilder->createSignedTransaction(
        _keyPair, _groupID, chainID, _to, _data, _abi, _blockLimit, _attribute, _extraData);

    auto& transactionHash = result.first;
    auto& signedTransaction = result.second;

    if (c_fileLogLevel <= LogLevel::TRACE)
    {
        BCOS_LOG(TRACE) << LOG_BADGE("JsonRpcServiceImpl::sendTransaction")
                        << LOG_KV("sign account", _keyPair.publicKey()->hex())
                        << LOG_KV("groupID", _groupID) << LOG_KV("chainID", chainID)
                        << LOG_KV("nodeName", _nodeName) << LOG_KV("to", _to) << LOG_KV("abi", _abi)
                        << LOG_KV("blockLimit", _blockLimit) << LOG_KV("attribute", _attribute)
                        << LOG_KV("extraData", _extraData)
                        << LOG_KV("transactionHash", transactionHash);
    }

    m_rpc->sendTransaction(_groupID, _nodeName, signedTransaction, false, std::move(_respFunc));
    return transactionHash;
}
