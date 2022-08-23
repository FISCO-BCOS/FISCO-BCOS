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
 * @brief status for the block sync
 * @file BlockSyncStatueInterface.h
 * @author: yujiechen
 * @date 2021-05-23
 */

#pragma once
#include "bcos-sync/interfaces/BlockSyncMsgInterface.h"
#include <bcos-framework/protocol/ProtocolTypeDef.h>
namespace bcos
{
namespace sync
{
class BlockSyncStatusInterface : virtual public BlockSyncMsgInterface
{
public:
    using Ptr = std::shared_ptr<BlockSyncStatusInterface>;
    using ConstPtr = std::shared_ptr<BlockSyncStatusInterface const>;
    BlockSyncStatusInterface() = default;
    virtual ~BlockSyncStatusInterface() {}

    virtual bcos::crypto::HashType const& hash() const = 0;
    virtual bcos::crypto::HashType const& genesisHash() const = 0;

    virtual void setHash(bcos::crypto::HashType const& _hash) = 0;
    virtual void setGenesisHash(bcos::crypto::HashType const& _gensisHash) = 0;
};
}  // namespace sync
}  // namespace bcos