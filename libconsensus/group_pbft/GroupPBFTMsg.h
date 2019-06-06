/*
 * @CopyRight:
 * FISCO-BCOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FISCO-BCOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2019 fisco-dev contributors.
 */

/**
 * @brief : implementation of Grouped PBFT Message
 * @file: GroupPBFTMsg.h
 * @author: yujiechen
 * @date: 2019-5-28
 */
#pragma once
#include <libconsensus/pbft/Common.h>

namespace dev
{
namespace consensus
{
struct GroupPBFTPacketType : public PBFTPacketType
{
    static const int SuperSignReqPacket = 0x05;
    static const int SuperCommitReqPacket = 0x06;
};

struct SuperSignReq : public PBFTMsg
{
    SuperSignReq() {}
    // construct SuperSignReq from the given prepare request
    SuperSignReq(
        std::shared_ptr<PrepareReq> prepareReq, VIEWTYPE const& globalView, IDXTYPE const& nodeIdx)
    {
        idx = nodeIdx;
        block_hash = prepareReq->block_hash;
        height = prepareReq->height;
        view = globalView;
        timestamp = u256(utcTime());
    }
    std::string uniqueKey() const override
    {
        auto uniqueKey = std::to_string(idx) + "_" + block_hash.hex() + "_" + std::to_string(view);
        return uniqueKey;
    }
};

struct SuperCommitReq : public SuperSignReq
{
    SuperCommitReq() {}
    // construct SuperCommitReq from the given prepare request
    SuperCommitReq(
        std::shared_ptr<PrepareReq> prepareReq, VIEWTYPE const& globalView, IDXTYPE const& nodeIdx)
      : SuperSignReq(prepareReq, globalView, nodeIdx)
    {}
};

}  // namespace consensus
}  // namespace dev