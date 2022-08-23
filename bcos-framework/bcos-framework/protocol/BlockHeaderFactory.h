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
 * @brief factory for BlockHeader
 * @file BlockHeaderFactory.h
 * @author: yujiechen
 * @date: 2021-03-23
 */
#pragma once
#include "BlockHeader.h"
namespace bcos
{
namespace protocol
{
class BlockHeaderFactory
{
public:
    using Ptr = std::shared_ptr<BlockHeaderFactory>;
    BlockHeaderFactory() = default;
    virtual ~BlockHeaderFactory() {}
    virtual BlockHeader::Ptr createBlockHeader() = 0;
    virtual BlockHeader::Ptr createBlockHeader(bytes const& _data) = 0;
    virtual BlockHeader::Ptr createBlockHeader(bytesConstRef _data) = 0;
    virtual BlockHeader::Ptr createBlockHeader(BlockNumber _number) = 0;

    virtual BlockHeader::Ptr populateBlockHeader(BlockHeader::Ptr _blockHeader)
    {
        auto header = createBlockHeader();
        header->setVersion(_blockHeader->version());
        header->setTxsRoot(_blockHeader->txsRoot());
        header->setReceiptsRoot(_blockHeader->receiptsRoot());
        header->setStateRoot(_blockHeader->stateRoot());
        header->setNumber(_blockHeader->number());
        header->setGasUsed(_blockHeader->gasUsed());
        header->setTimestamp(_blockHeader->timestamp());
        header->setSealer(_blockHeader->sealer());
        header->setSealerList(_blockHeader->sealerList());
        header->setSignatureList(_blockHeader->signatureList());
        header->setConsensusWeights(_blockHeader->consensusWeights());
        header->setParentInfo(_blockHeader->parentInfo());
        auto extraData = _blockHeader->extraData().toBytes();
        header->setExtraData(std::move(extraData));
        return header;
    }
};
}  // namespace protocol
}  // namespace bcos