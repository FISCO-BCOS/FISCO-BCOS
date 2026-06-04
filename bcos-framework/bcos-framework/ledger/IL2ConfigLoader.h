/**
 *  Copyright (C) 2024 FISCO BCOS.
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
 * @file IL2ConfigLoader.h
 * @brief Interface that bcos-ledger consumes to refresh LedgerConfig from the
 *        L2 SystemConfig predeploy each block. Lives in bcos-framework so the
 *        ledger has no reverse dependency on the bcos-executor implementation.
 */
#pragma once
#include "LedgerConfig.h"
#include <bcos-framework/protocol/ProtocolTypeDef.h>
#include <bcos-task/Task.h>
#include <memory>

namespace bcos::ledger
{
class IL2ConfigLoader
{
public:
    using Ptr = std::shared_ptr<IL2ConfigLoader>;
    IL2ConfigLoader() = default;
    IL2ConfigLoader(const IL2ConfigLoader&) = default;
    IL2ConfigLoader(IL2ConfigLoader&&) = default;
    IL2ConfigLoader& operator=(const IL2ConfigLoader&) = default;
    IL2ConfigLoader& operator=(IL2ConfigLoader&&) = default;
    virtual ~IL2ConfigLoader() = default;

    /// Refresh @p out from the L2 SystemConfig predeploy for @p blockNumber.
    /// On failure (revert / short return / OOG) the implementation MUST throw so
    /// the caller aborts the current block; it must never swallow the error and
    /// fall back to a cached config.
    virtual task::Task<void> loadIntoLedgerConfig(
        protocol::BlockNumber blockNumber, LedgerConfig& out) = 0;
};
}  // namespace bcos::ledger
