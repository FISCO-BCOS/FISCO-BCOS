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
 * @brief Initializer for the ledger
 * @file LedgerInitializer.h
 * @author: yujiechen
 * @date 2021-06-10
 */
#pragma once
#include <bcos-framework/ledger/LedgerInterface.h>
#include <bcos-framework/protocol/BlockFactory.h>
#include <bcos-framework/storage/StorageInterface.h>
#include <bcos-ledger/src/libledger/LedgerImpl.h>
#include <bcos-storage/bcos-storage/StorageImpl.h>
#include <bcos-tool/NodeConfig.h>

namespace bcos::initializer
{
class LedgerInitializer
{
public:
    static std::shared_ptr<bcos::ledger::Ledger> build(
        bcos::protocol::BlockFactory::Ptr blockFactory,
        bcos::storage::StorageInterface::Ptr storage, bcos::tool::NodeConfig::Ptr nodeConfig)
    {
        bcos::storage::StorageImpl storageWrapper(storage);

        if (nodeConfig->smCryptoType())
        {
            auto ledger = std::make_shared<bcos::ledger::LedgerImpl<
                bcos::crypto::hasher::openssl::OpenSSL_SM3_Hasher, decltype(storageWrapper)>>(
                std::move(storageWrapper), blockFactory, storage);
            ledger->buildGenesisBlock(nodeConfig->ledgerConfig(), nodeConfig->txGasLimit(),
                nodeConfig->genesisData(), nodeConfig->compatibilityVersionStr());

            return ledger;
        }

        auto ledger = std::make_shared<bcos::ledger::LedgerImpl<
            bcos::crypto::hasher::openssl::OpenSSL_Keccak256_Hasher, decltype(storageWrapper)>>(
            std::move(storageWrapper), blockFactory, storage);
        ledger->buildGenesisBlock(nodeConfig->ledgerConfig(), nodeConfig->txGasLimit(),
            nodeConfig->genesisData(), nodeConfig->compatibilityVersionStr());

        return ledger;
    }
};
}  // namespace bcos::initializer
