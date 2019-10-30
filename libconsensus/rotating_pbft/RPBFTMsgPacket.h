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
 * @file: RPBFTMsgPacket.h
 * @author: yujiechen
 * @date: 2019-10-28
 */
#pragma once
#include <libconsensus/pbft/Common.h>

namespace dev
{
namespace consensus
{
class RPBFTMsgPacket : public PBFTMsgPacket
{
public:
    // the node that disconnected from this node, but the packet should reach
    dev::h512s forwardNodes;

    using Ptr = std::shared_ptr<RPBFTMsgPacket>;
    RPBFTMsgPacket() : PBFTMsgPacket(), forwardNodes(dev::h512s()) {}

    void streamRLPFields(RLPStream& _s) const override
    {
        PBFTMsgPacket::streamRLPFields(_s);
        _s.appendVector(forwardNodes);
    }

    void populate(RLP const& _rlp) override
    {
        try
        {
            PBFTMsgPacket::populate(_rlp);
            forwardNodes = _rlp[3].toVector<dev::h512>();
        }
        catch (Exception const& e)
        {
            e << dev::eth::errinfo_name("invalid msg format");
            throw;
        }
    }

    void setForwardNodes(dev::h512s const& _forwardNodes) { forwardNodes = _forwardNodes; }
};
}  // namespace consensus
}  // namespace dev
