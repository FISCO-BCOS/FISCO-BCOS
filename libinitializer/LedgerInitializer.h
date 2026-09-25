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
#include "AddressTableModeDetection.h"
#include "bcos-ledger/Ledger.h"
#include <bcos-framework/protocol/BlockFactory.h>
#include <bcos-framework/storage/StorageInterface.h>
#include <bcos-tool/NodeConfig.h>
#include <bcos-utilities/IOServicePool.h>
#include <optional>

namespace bcos::initializer
{
/// The on-chain executor_version and its activation block, read once for both the lane
/// wiring (Initializer) and the OP-mode boot invariant (validateOpModeGenesisOnly).
struct OnChainExecutorVersion
{
    int version = 0;
    bcos::protocol::BlockNumber activation = 0;
    bool present = false;
};

/// Read executor_version from the ledger. A row that exists but does not parse is a boot
/// failure with a diagnosable message (never a bare boost::bad_lexical_cast out of the
/// initializer); an absent row falls back to the genesis config's value.
OnChainExecutorVersion readOnChainExecutorVersion(
    bcos::ledger::LedgerInterface& ledger, int fallbackVersion);

class LedgerInitializer
{
public:
    /// @param accountTableBoot the boot-time account-table handling (AccountTableBoot): lane
    ///        check → layout-flag read (one point Get) → optional one-shot hex→binary
    ///        migration → node-mode publication, all before buildGenesisBlock. Nullopt skips
    ///        the whole sequence (tools that never serve execution, e.g. archive-tool): the
    ///        singleton keeps its Hex default, the pre-detection behavior.
    static std::shared_ptr<bcos::ledger::Ledger> build(
        bcos::protocol::BlockFactory::Ptr blockFactory,
        bcos::storage::StorageInterface::Ptr storage, bcos::tool::NodeConfig::Ptr nodeConfig,
        bcos::storage::StorageInterface::Ptr blockStorage,
        bcos::IOServicePool::Ptr ioServicePool = nullptr,
        std::optional<AccountTableBoot> accountTableBoot = std::nullopt);
};
}  // namespace bcos::initializer
