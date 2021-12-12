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
 * @brief interface for the message contains blockData
 * @file BlocksMsgInterface.h
 * @author: yujiechen
 * @date 2021-05-24
 */
#pragma once
#include "bcos-sync/interfaces/BlockSyncMsgInterface.h"
namespace bcos
{
namespace sync
{
class BlocksMsgInterface : virtual public BlockSyncMsgInterface
{
public:
    using Ptr = std::shared_ptr<BlocksMsgInterface>;
    BlocksMsgInterface() = default;
    virtual ~BlocksMsgInterface() {}

    virtual size_t blocksSize() const = 0;
    virtual bytesConstRef blockData(size_t _index) const = 0;

    virtual void appendBlockData(bytes&& _blockData) = 0;
    virtual void appendBlockData(bytes const& _blockData) = 0;
};
using BlocksMsgList = std::vector<BlocksMsgInterface::Ptr>;
using BlocksMsgListPtr = std::shared_ptr<BlocksMsgList>;
}  // namespace sync
}  // namespace bcos