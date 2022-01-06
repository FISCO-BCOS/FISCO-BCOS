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
 * @file ParallelConfigPrecompiled.h
 * @author: kyonRay
 * @date 2021-05-28
 */

#pragma once
#include "../executive/BlockContext.h"
#include "../vm/Precompiled.h"
#include "Common.h"
#include <bcos-codec/abi/ContractABICodec.h>
#include <bcos-framework/interfaces/storage/Table.h>

namespace bcos
{
namespace precompiled
{
struct ParallelConfig
{
    using Ptr = std::shared_ptr<ParallelConfig>;
    std::string functionName;
    u256 criticalSize;

    template <typename Archive>
    void serialize(Archive& ar, const unsigned int version)
    {
        boost::ignore_unused(version);
        ar& functionName;
        ar& criticalSize;
    }
};
const std::string PARA_CONFIG_TABLE_PREFIX_SHORT = "cp_";

/*
    table name: PARA_CONFIG_TABLE_PREFIX_CONTRACT_ADDR_
    | selector   | functionName                    | criticalSize |
    | ---------- | ------------------------------- | ------------ |
    | 0x12345678 | transfer(string,string,uint256) | 2            |
    | 0x23456789 | set(string,uint256)             | 1            |
*/

const std::string PARA_SELECTOR = "selector";
const std::string PARA_FUNC_NAME = "functionName";
const std::string PARA_CRITICAL_SIZE = "criticalSize";

const std::string PARA_CONFIG_REGISTER_METHOD_ADDR_STR_UINT =
    "registerParallelFunctionInternal(address,string,uint256)";
const std::string PARA_CONFIG_REGISTER_METHOD_STR_STR_UINT =
    "registerParallelFunctionInternal(string,string,uint256)";
const std::string PARA_CONFIG_UNREGISTER_METHOD_ADDR_STR =
    "unregisterParallelFunctionInternal(address,string)";
const std::string PARA_CONFIG_UNREGISTER_METHOD_STR_STR =
    "unregisterParallelFunctionInternal(string,string)";

const std::string PARA_KEY_NAME = PARA_SELECTOR;
const std::string PARA_VALUE_NAMES = PARA_FUNC_NAME + "," + PARA_CRITICAL_SIZE;


class ParallelConfigPrecompiled : public bcos::precompiled::Precompiled
{
public:
    using Ptr = std::shared_ptr<ParallelConfigPrecompiled>;
    ParallelConfigPrecompiled(crypto::Hash::Ptr _hashImpl);
    virtual ~ParallelConfigPrecompiled(){};

    std::string toString() override;

    std::shared_ptr<PrecompiledExecResult> call(
        std::shared_ptr<executor::TransactionExecutive> _executive, bytesConstRef _param,
        const std::string& _origin, const std::string& _sender) override;

    std::shared_ptr<bcos::storage::Table> openTable(
        std::shared_ptr<executor::TransactionExecutive> _executive,
        std::string const& _contractAddress, std::string const& _origin, bool _needCreate = true);

private:
    void registerParallelFunction(PrecompiledCodec::Ptr _codec,
        std::shared_ptr<executor::TransactionExecutive> _executive, bytesConstRef _data,
        std::string const& _origin, bytes& _out);
    void unregisterParallelFunction(PrecompiledCodec::Ptr _codec,
        std::shared_ptr<executor::TransactionExecutive> _executive, bytesConstRef _data,
        std::string const& _origin, bytes& _out);
    std::string getTableName(std::string_view const& _contractName);

public:
    /// get parallel config, return nullptr if not found
    ParallelConfig::Ptr getParallelConfig(
        std::shared_ptr<executor::TransactionExecutive> _executive,
        const std::string_view& _contractAddress, uint32_t _selector,
        const std::string_view& _origin);
};
}  // namespace precompiled
}  // namespace bcos