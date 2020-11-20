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
 * (c) 2016-2018 fisco-dev contributors.
 */

/**
 * @brief : common functions and types of Raft consenus module
 * @file: Common.h
 * @author: catli
 * @date: 2018-12-05
 */

#pragma once

#include <libconsensus/Common.h>
#include <libdevcrypto/Common.h>
#include <libethcore/Exceptions.h>
#include <libutilities/RLP.h>
#include <libutilities/concurrent_queue.h>

#define RAFTENGINE_LOG(LEVEL) LOG(LEVEL) << LOG_BADGE("CONSENSUS") << LOG_BADGE("RAFTENGINE")
#define RAFTSEALER_LOG(LEVEL) LOG(LEVEL) << LOG_BADGE("CONSENSUS") << LOG_BADGE("RAFTSEALER")

namespace bcos
{
namespace consensus
{
namespace raft
{
using NodeIndex = bcos::consensus::IDXTYPE;
}  // namespace raft

enum RaftPacketType : byte
{
    RaftVoteReqPacket = 0x00,
    RaftVoteRespPacket = 0x01,
    RaftHeartBeatPacket = 0x02,
    RaftHeartBeatRespPacket = 0x03,
    RaftPacketCount
};

enum RaftRole : byte
{
    EN_STATE_LEADER = 1,
    EN_STATE_FOLLOWER = 2,
    EN_STATE_CANDIDATE = 3
};

enum VoteRespFlag : byte
{
    VOTE_RESP_REJECT = 0,
    VOTE_RESP_LEADER_REJECT = 1,
    VOTE_RESP_LASTTERM_ERROR = 2,
    VOTE_RESP_OUTDATED = 3,
    VOTE_RESP_FIRST_VOTE = 4,
    VOTE_RESP_DISCARD = 5,
    VOTE_RESP_GRANTED = 6
};

enum HandleVoteResult : byte
{
    TO_LEADER,
    TO_FOLLOWER,
    NONE
};

struct RaftMsgPacket
{
    /// the index of the node that send this raft message
    raft::NodeIndex nodeIdx;
    /// the node id of the node that sends this raft message
    Public nodeId;
    /// type of the packet
    RaftPacketType packetType;
    /// the data of concrete request
    bytes data;
    /// timestamp of receive this raft message
    uint64_t timestamp;
    /// endpoint
    std::string endpoint;

    RaftMsgPacket()
      : nodeIdx(0),
        nodeId(Public(0)),
        packetType(RaftPacketType::RaftVoteReqPacket),
        timestamp(utcTime())
    {}
    virtual ~RaftMsgPacket() = default;
    virtual void encode(bytes& _encodeBytes) const
    {
        RLPStream tmp;
        streamRLPFields(tmp);
        tmp.swapOut(_encodeBytes);
    }

    virtual void decode(bytesConstRef _data) { populate(_data); }

    void streamRLPFields(RLPStream& stream) const
    {
        stream.append(unsigned(packetType)).append(data);
    }

    void populate(bytesConstRef _reqData)
    {
        try
        {
            packetType =
                (RaftPacketType)RLP(_reqData.cropped(0, 1)).toInt<unsigned>(RLP::ThrowOnFail);
            data = RLP(_reqData.cropped(1)).toBytes();
        }
        catch (Exception const& e)
        {
            e << bcos::eth::errinfo_name("invalid msg format");
            throw;
        }
    }

    void setOtherField(u256 const& _nodeIdx, Public const& _nodeId, std::string const& _endpoint)
    {
        nodeIdx = static_cast<raft::NodeIndex>(_nodeIdx);
        nodeId = _nodeId;
        endpoint = _endpoint;
        timestamp = utcTime();
    }
};

using RaftMsgQueue = bcos::concurrent_queue<RaftMsgPacket>;

struct RaftMsg
{
    raft::NodeIndex idx;
    size_t term;
    int64_t height;
    h256 blockHash;
    virtual ~RaftMsg() = default;
    virtual void streamRLPFields(RLPStream& _s) const { _s << idx << term << height << blockHash; }
    virtual void populate(RLP const& _rlp)
    {
        int field = 0;
        try
        {
            idx = _rlp[field = 0].toInt<raft::NodeIndex>();
            term = _rlp[field = 1].toInt<size_t>();
            height = _rlp[field = 2].toInt<int64_t>();
            blockHash = _rlp[field = 3].toHash<h256>(RLP::VeryStrict);
        }
        catch (Exception const& _e)
        {
            _e << bcos::eth::errinfo_name("invalid msg format")
               << bcos::eth::BadFieldError(field, toHex(_rlp[field].data().toBytes()));
            throw;
        }
    }
};

struct RaftVoteReq : public RaftMsg
{
    raft::NodeIndex candidate;
    size_t lastLeaderTerm;
    int64_t lastBlockNumber;

    virtual void streamRLPFields(RLPStream& _s) const
    {
        RaftMsg::streamRLPFields(_s);
        _s << candidate << lastLeaderTerm << lastBlockNumber;
    }

    virtual void populate(RLP const& _rlp)
    {
        RaftMsg::populate(_rlp);
        int field = 0;
        try
        {
            candidate = _rlp[field = 4].toInt<raft::NodeIndex>();
            lastLeaderTerm = _rlp[field = 5].toInt<size_t>();
            lastBlockNumber = _rlp[field = 6].toInt<int64_t>();
        }
        catch (Exception const& _e)
        {
            _e << bcos::eth::errinfo_name("invalid msg format")
               << bcos::eth::BadFieldError(field, toHex(_rlp[field].data().toBytes()));
            throw;
        }
    }
};

struct VoteState
{
    size_t vote = 0;
    size_t unVote = 0;
    size_t lastTermErr = 0;
    size_t firstVote = 0;
    size_t discardedVote = 0;
    size_t outdated = 0;

    uint64_t totalVoteCount()
    {
        return vote + unVote + lastTermErr + firstVote + discardedVote + outdated;
    }
};

struct RaftVoteResp : public RaftMsg
{
    VoteRespFlag voteFlag;
    size_t lastLeaderTerm;

    virtual void streamRLPFields(RLPStream& _s) const
    {
        RaftMsg::streamRLPFields(_s);
        _s << voteFlag << lastLeaderTerm;
    }

    virtual void populate(RLP const& _rlp)
    {
        RaftMsg::populate(_rlp);
        int field = 0;
        try
        {
            voteFlag = static_cast<VoteRespFlag>(_rlp[field = 4].toInt<byte>());
            lastLeaderTerm = _rlp[field = 5].toInt<size_t>();
        }
        catch (Exception const& _e)
        {
            _e << bcos::eth::errinfo_name("invalid msg format")
               << bcos::eth::BadFieldError(field, toHex(_rlp[field].data().toBytes()));
            throw;
        }
    }
};

struct RaftHeartBeat : public RaftMsg
{
    raft::NodeIndex leader;
    bytes uncommitedBlock;
    int64_t uncommitedBlockNumber;

    bool hasData() const { return uncommitedBlock.size() != 0; }

    virtual void streamRLPFields(RLPStream& _s) const
    {
        RaftMsg::streamRLPFields(_s);
        _s << leader << uncommitedBlock << uncommitedBlockNumber;
    }

    virtual void populate(RLP const& _rlp)
    {
        RaftMsg::populate(_rlp);
        int field = 0;
        try
        {
            leader = _rlp[field = 4].toInt<raft::NodeIndex>();
            uncommitedBlock = _rlp[field = 5].toBytes();
            uncommitedBlockNumber = _rlp[field = 6].toInt<int64_t>();
        }
        catch (Exception const& _e)
        {
            _e << bcos::eth::errinfo_name("invalid msg format")
               << bcos::eth::BadFieldError(field, toHex(_rlp[field].data().toBytes()));
            throw;
        }
    }
};

struct RaftHeartBeatResp : public RaftMsg
{
    h256 uncommitedBlockHash;

    virtual void streamRLPFields(RLPStream& _s) const
    {
        RaftMsg::streamRLPFields(_s);
        _s << uncommitedBlockHash;
    }

    virtual void populate(RLP const& _rlp)
    {
        RaftMsg::populate(_rlp);
        int field = 0;
        try
        {
            uncommitedBlockHash = _rlp[field = 4].toHash<bcos::h256>();
        }
        catch (Exception const& _e)
        {
            _e << bcos::eth::errinfo_name("invalid msg format")
               << bcos::eth::BadFieldError(field, toHex(_rlp[field].data().toBytes()));
            throw;
        }
    }
};
}  // namespace consensus
}  // namespace bcos
