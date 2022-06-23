/**
 *  copyright (C) 2021 FISCO BCOS.
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
 * @file SmallBankPrecompiled.h
 * @author: wenlinli
 * @date 2022-03-17
 */

#pragma once

#include "../../vm/Precompiled.h"
#include "bcos-executor/src/precompiled/common/Common.h"
#include <bcos-framework//ledger/LedgerTypeDef.h>
#include <bcos-framework//storage/Table.h>

namespace bcos
{
namespace precompiled
{
class SmallBankPrecompiled : public bcos::precompiled::Precompiled
{
public:
    using Ptr = std::shared_ptr<SmallBankPrecompiled>;

    SmallBankPrecompiled(crypto::Hash::Ptr _hashImpl, std::string _tableName);

    virtual ~SmallBankPrecompiled(){};

    std::shared_ptr<PrecompiledExecResult> call(
        std::shared_ptr<executor::TransactionExecutive> _executive,
        PrecompiledExecResult::Ptr _callParameters) override;

public:
    // is this precompiled need parallel processing, default false.
    bool isParallelPrecompiled() override { return true; }
    std::vector<std::string> getParallelTag(bytesConstRef param, bool _isWasm) override;

    static inline std::string getAddress(unsigned int id)
    {
        u160 address = u160(SMALLBANK_START_ADDRESS);
        address += id;
        h160 addressBytes = h160(address);
        return addressBytes.hex();
    }

    static void registerPrecompiled(
        std::shared_ptr<std::map<std::string, std::shared_ptr<precompiled::Precompiled>>>
            registeredMap,
        crypto::Hash::Ptr _hashImpl)
    {
        for (int id = 0; id < SMALLBANK_CONTRACT_NUM; id++)
        {
            std::string _tableName = std::to_string(id);
            std::string path = bcos::ledger::SMALLBANK_TRANSFER;
            _tableName = path + _tableName;
            std::string&& address = getAddress(id);
            registeredMap->insert({std::move(address),
                std::make_shared<precompiled::SmallBankPrecompiled>(_hashImpl, _tableName)});
        }
        BCOS_LOG(TRACE) << LOG_BADGE("SmallBank") << "Register SmallBankPrecompiled complete"
                        << LOG_KV("addressFrom", getAddress(0))
                        << LOG_KV("addressTo", getAddress(SMALLBANK_CONTRACT_NUM - 1))
                        << LOG_KV("tableNameBase", bcos::ledger::SMALLBANK_TRANSFER);
    }

public:
    void updateBalanceCall(std::shared_ptr<executor::TransactionExecutive> _executive,
        bytesConstRef _data, std::string const& _origin, bytes& _out);
    void sendPaymentCall(std::shared_ptr<executor::TransactionExecutive> _executive,
        bytesConstRef _data, std::string const& _origin, bytes& _out);

private:
    std::string m_tableName;
};
}  // namespace precompiled
}  // namespace bcos
