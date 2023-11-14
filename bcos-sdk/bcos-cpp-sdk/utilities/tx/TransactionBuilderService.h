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
 * @file TransactionBuilderService.h
 * @author: octopus
 * @date 2022-01-13
 */
#pragma once
#include <bcos-cpp-sdk/Sdk.h>
#include <bcos-cpp-sdk/utilities/tx/TransactionBuilder.h>
#include <bcos-cpp-sdk/ws/Service.h>
#include <bcos-utilities/Common.h>
#include <cstddef>
#include <memory>

namespace bcos
{
namespace cppsdk
{
namespace utilities
{
class TransactionBuilderService
{
public:
    using Ptr = std::shared_ptr<TransactionBuilderService>;
    using ConstPtr = std::shared_ptr<const TransactionBuilderService>;
    using UniquePtr = std::unique_ptr<TransactionBuilderService>;

public:
    TransactionBuilderService(service::Service::Ptr _service, const std::string& _groupID,
        TransactionBuilderInterface::Ptr _transactionBuilderInterface = nullptr);

public:
    /**
     * @brief Create a Transaction Data object
     *
     * @param _to
     * @param _data
     * @param _abi
     * @return bcostars::TransactionDataUniquePtr
     */
    bcostars::TransactionDataUniquePtr createTransactionData(
        const std::string& _to, const bcos::bytes& _data, const std::string& _abi = "");

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
    std::pair<std::string, std::string> createSignedTransaction(
        const bcos::crypto::KeyPairInterface& _keyPair, const std::string& _to,
        const bcos::bytes& _data, const std::string& _abi, int32_t _attribute,
        const std::string& _extraData = "");

private:
    service::Service::Ptr m_service;
    bcos::group::GroupInfo::Ptr m_groupInfo;

    TransactionBuilderInterface::Ptr m_transactionBuilderInterface = nullptr;
};
}  // namespace utilities
}  // namespace cppsdk
}  // namespace bcos