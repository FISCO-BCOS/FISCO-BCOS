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
 * @file TransactionBuilderService.cpp
 * @author: octopus
 * @date 2022-01-13
 */
#include <bcos-cpp-sdk/utilities/Common.h>
#include <bcos-cpp-sdk/utilities/tx/TransactionBuilderService.h>
#include <bcos-utilities/BoostLog.h>
#include <memory>
#include <string>

using namespace bcos;
using namespace bcos::cppsdk;
using namespace bcos::cppsdk::utilities;

TransactionBuilderService::TransactionBuilderService(service::Service::Ptr _service,
    const std::string& _groupID, TransactionBuilderInterface::Ptr _transactionBuilderInterface)
{
    auto groupInfo = _service->getGroupInfo(_groupID);
    if (!groupInfo)
    {
        UTILITIES_TX_LOG(WARNING) << LOG_BADGE("TransactionBuilderService")
                                  << LOG_DESC("group not exist") << LOG_KV("groupID", _groupID);
        BOOST_THROW_EXCEPTION(std::runtime_error("group not exist, groupID: " + _groupID));
    }

    UTILITIES_TX_LOG(INFO) << LOG_BADGE("TransactionBuilderService") << LOG_DESC("init group info")
                           << LOG_KV("groupID", groupInfo->groupID())
                           << LOG_KV("chainID", groupInfo->chainID())
                           << LOG_KV("smCryptoType", groupInfo->smCryptoType())
                           << LOG_KV("isWasm", groupInfo->wasm());

    m_service = _service;
    m_groupInfo = groupInfo;
    if (_transactionBuilderInterface)
    {
        // default builder
        m_transactionBuilderInterface = std::make_shared<TransactionBuilder>();
    }
    else
    {
        m_transactionBuilderInterface = _transactionBuilderInterface;
    }
}

/**
 * @brief Create a Transaction Data object
 *
 * @param _to
 * @param _data
 * @param _abi
 * @return bcostars::TransactionDataUniquePtr
 */
bcostars::TransactionDataUniquePtr TransactionBuilderService::createTransactionData(
    const std::string& _to, const bcos::bytes& _data, const std::string& _abi)
{
    int64_t blockLimit = 0;
    if (!m_service->getBlockLimit(m_groupInfo->groupID(), blockLimit))
    {
        BOOST_THROW_EXCEPTION(
            std::runtime_error("get block limit failed, groupID: " + m_groupInfo->groupID()));
    }

    return m_transactionBuilderInterface->createTransactionData(
        m_groupInfo->groupID(), m_groupInfo->chainID(), _to, _data, _abi, blockLimit);
}

/**
 * @brief Create a Signed Transaction object
 *
 * @param _keyPair
 * @param _to
 * @param _data
 * @param _abi
 * @param _attribute
 * @param _extraData
 * @return std::pair<std::string, std::string>
 */
std::pair<std::string, std::string> TransactionBuilderService::createSignedTransaction(
    const bcos::crypto::KeyPairInterface& _keyPair, const std::string& _to,
    const bcos::bytes& _data, const std::string& _abi, int32_t _attribute,
    const std::string& _extraData)
{
    int64_t blockLimit = 0;
    if (!m_service->getBlockLimit(m_groupInfo->groupID(), blockLimit))
    {
        BOOST_THROW_EXCEPTION(
            std::runtime_error("get block limit failed, groupID: " + m_groupInfo->groupID()));
    }

    auto keyPairStrFunc = [](auto type) {
        if (type == bcos::crypto::KeyPairType::SM2 || type == bcos::crypto::KeyPairType::HsmSM2)
        {
            return "sm";
        }
        else if (type == bcos::crypto::KeyPairType::Secp256K1)
        {
            return "none sm";
        }

        return "unkown";
    };

    // check keypair type
    auto keyPairType = _keyPair.keyPairType();
    if (m_groupInfo->smCryptoType() && keyPairType != bcos::crypto::KeyPairType::SM2 &&
        keyPairType != bcos::crypto::KeyPairType::HsmSM2)
    {
        BOOST_THROW_EXCEPTION(std::runtime_error(
            "group id: " + m_groupInfo->groupID() + " ,group crypto type: sm/hsm" +
            " ,but the keypair type: " + keyPairStrFunc(keyPairType)));
    }
    else if (!m_groupInfo->smCryptoType() && keyPairType != bcos::crypto::KeyPairType::Secp256K1)
    {
        BOOST_THROW_EXCEPTION(std::runtime_error(
            "group id: " + m_groupInfo->groupID() + " ,group crypto type: none sm" +
            " ,but the keypair type: " + keyPairStrFunc(keyPairType)));
    }

    return m_transactionBuilderInterface->createSignedTransaction(_keyPair, m_groupInfo->groupID(),
        m_groupInfo->chainID(), _to, _data, _abi, blockLimit, _attribute, _extraData);
}
