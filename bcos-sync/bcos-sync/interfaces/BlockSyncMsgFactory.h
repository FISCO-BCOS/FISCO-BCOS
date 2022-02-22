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
 * @file BlockSyncMsgFactory.h
 * @author: yujiechen
 * @date 2021-05-23
 */
#pragma once
#include "bcos-sync/interfaces/BlockRequestInterface.h"
#include "bcos-sync/interfaces/BlockSyncStatusInterface.h"
#include "bcos-sync/interfaces/BlocksMsgInterface.h"
namespace bcos
{
namespace sync
{
class BlockSyncMsgFactory
{
public:
    using Ptr = std::shared_ptr<BlockSyncMsgFactory>;
    BlockSyncMsgFactory() = default;
    virtual ~BlockSyncMsgFactory() {}

    virtual BlockSyncMsgInterface::Ptr createBlockSyncMsg(bytesConstRef _data) = 0;
    virtual BlockSyncStatusInterface::Ptr createBlockSyncStatusMsg() = 0;
    virtual BlockSyncStatusInterface::Ptr createBlockSyncStatusMsg(bytesConstRef _data) = 0;
    virtual BlockSyncStatusInterface::Ptr createBlockSyncStatusMsg(
        BlockSyncMsgInterface::Ptr _msg) = 0;
    virtual BlockSyncStatusInterface::Ptr createBlockSyncStatusMsg(
        bcos::protocol::BlockNumber _number, bcos::crypto::HashType const& _hash,
        bcos::crypto::HashType const& _gensisHash, int32_t _version = 0)
    {
        auto statusMsg = createBlockSyncStatusMsg();
        statusMsg->setVersion(_version);
        statusMsg->setNumber(_number);
        statusMsg->setHash(_hash);
        statusMsg->setGenesisHash(_gensisHash);
        return statusMsg;
    }

    virtual BlocksMsgInterface::Ptr createBlocksMsg() = 0;
    virtual BlocksMsgInterface::Ptr createBlocksMsg(bytesConstRef _data) = 0;
    virtual BlocksMsgInterface::Ptr createBlocksMsg(BlockSyncMsgInterface::Ptr _msg) = 0;

    virtual BlockRequestInterface::Ptr createBlockRequest() = 0;
    virtual BlockRequestInterface::Ptr createBlockRequest(bytesConstRef _data) = 0;
    virtual BlockRequestInterface::Ptr createBlockRequest(BlockSyncMsgInterface::Ptr _msg) = 0;
};
}  // namespace sync
}  // namespace bcos