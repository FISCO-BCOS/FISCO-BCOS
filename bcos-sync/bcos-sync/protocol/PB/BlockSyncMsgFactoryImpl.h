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
 * @brief factory to create block sync message
 * @file BlockSyncMsgFactoryImpl.h
 * @author: yujiechen
 * @date 2021-05-23
 */
#pragma once
#include "bcos-sync/interfaces/BlockSyncMsgFactory.h"
#include "bcos-sync/protocol/PB/BlockRequestImpl.h"
#include "bcos-sync/protocol/PB/BlockSyncStatusImpl.h"
#include "bcos-sync/protocol/PB/BlocksMsgImpl.h"
namespace bcos
{
namespace sync
{
class BlockSyncMsgFactoryImpl : public BlockSyncMsgFactory
{
public:
    BlockSyncMsgFactoryImpl() = default;
    ~BlockSyncMsgFactoryImpl() override {}

    BlockSyncMsgInterface::Ptr createBlockSyncMsg(bytesConstRef _data) override
    {
        return std::make_shared<BlockSyncMsgImpl>(_data);
    }

    BlockSyncStatusInterface::Ptr createBlockSyncStatusMsg() override
    {
        return std::make_shared<BlockSyncStatusImpl>();
    }

    BlockSyncStatusInterface::Ptr createBlockSyncStatusMsg(bytesConstRef _data) override
    {
        return std::make_shared<BlockSyncStatusImpl>(_data);
    }
    BlockSyncStatusInterface::Ptr createBlockSyncStatusMsg(BlockSyncMsgInterface::Ptr _msg) override
    {
        auto syncMsg = std::dynamic_pointer_cast<BlockSyncMsgImpl>(_msg);
        return std::make_shared<BlockSyncStatusImpl>(syncMsg);
    }

    BlocksMsgInterface::Ptr createBlocksMsg() override { return std::make_shared<BlocksMsgImpl>(); }
    BlocksMsgInterface::Ptr createBlocksMsg(bytesConstRef _data) override
    {
        return std::make_shared<BlocksMsgImpl>(_data);
    }
    BlocksMsgInterface::Ptr createBlocksMsg(BlockSyncMsgInterface::Ptr _msg) override
    {
        auto syncMsg = std::dynamic_pointer_cast<BlockSyncMsgImpl>(_msg);
        return std::make_shared<BlocksMsgImpl>(syncMsg);
    }

    BlockRequestInterface::Ptr createBlockRequest() override
    {
        return std::make_shared<BlockRequestImpl>();
    }
    BlockRequestInterface::Ptr createBlockRequest(bytesConstRef _data) override
    {
        return std::make_shared<BlockRequestImpl>(_data);
    }
    BlockRequestInterface::Ptr createBlockRequest(BlockSyncMsgInterface::Ptr _msg) override
    {
        auto syncMsg = std::dynamic_pointer_cast<BlockSyncMsgImpl>(_msg);
        return std::make_shared<BlockRequestImpl>(syncMsg);
    }
};
}  // namespace sync
}  // namespace bcos