/*
    This file is part of cpp-ethereum.

    cpp-ethereum is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    cpp-ethereum is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with cpp-ethereum.  If not, see <http://www.gnu.org/licenses/>.
*/
/**
 * @file: Common.h
 * @author: fisco-dev
 *
 * @date: 2017
 * A proof of work algorithm.
 */

#pragma once

#include <libdevcore/RLP.h>
#include <libdevcore/concurrent_queue.h>
#include <libdevcore/easylog.h>
#include <libdevcrypto/Common.h>
#include <libethcore/Exceptions.h>

#define RAFT_LOG(LEVEL) LOG(LEVEL) << "[#LIBCONSENSUS][#RAFTENGINE] "
#define RAFTENGINE_LOG(LEVEL) \
    LOG(LEVEL) << "[#LIBCONSENSUS][#RAFTENGINE][PROTOCOL=" << m_protocolId << "]"
#define RAFTSEALER_LOG(LEVEL) \
    LOG(LEVEL) << "[#LIBCONSENSUS][#RAFTSEALER][PROTOCOL=" << m_raftEngine->protocolId() << "]"

namespace dev
{
namespace consensus
{
enum RaftPacketType : byte
{
    RaftVoteReqPacket = 0x00,
    RaftVoteRespPacket = 0x01,
    RaftHeartBeatPacket = 0x02,
    RaftHeartBeatRespPacket = 0x03,
    RaftPacketCount
};

enum RaftRole
{
    EN_STATE_LEADER = 1,
    EN_STATE_FOLLOWER = 2,
    EN_STATE_CANDIDATE = 3
};

enum VoteRespFlag
{
    VOTE_RESP_REJECT = 0,
    VOTE_RESP_LEADER_REJECT = 1,
    VOTE_RESP_LASTTERM_ERROR = 2,
    VOTE_RESP_FIRST_VOTE = 3,
    VOTE_RESP_DISCARD = 4,
    VOTE_RESP_GRANTED = 5
};

enum HandleVoteResult
{
    TO_LEADER,
    TO_FOLLOWER,
    NONE
};

struct RaftMsgPacket
{
    /// the index of the node that send this raft message
    u256 nodeIdx;
    /// the node id of the node that sends this raft message
    h512 nodeId;
    /// type of the packet
    RaftPacketType packetType;
    /// the data of concrete request
    bytes data;
    /// timestamp of receive this raft message
    u256 timestamp;
    /// endpoint
    std::string endpoint;

    RaftMsgPacket()
      : nodeIdx(h256(0)),
        nodeId(h512(0)),
        packetType(RaftPacketType::RaftVoteReqPacket),
        timestamp(u256(utcTime()))
    {}

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
            RAFT_LOG(DEBUG) << "[#RaftMsgPacket::populate] invalid msg format";

            e << dev::eth::errinfo_name("invalid msg format");
            throw;
        }
    }

    void setOtherField(u256 const& _nodeIdx, h512 const& _nodeId, std::string const& _endpoint)
    {
        nodeIdx = _nodeIdx;
        nodeId = _nodeId;
        endpoint = _endpoint;
        timestamp = u256(utcTime());
    }
};

using RaftMsgQueue = dev::concurrent_queue<RaftMsgPacket>;

struct RaftMsg
{
    u256 idx;
    u256 term;
    u256 height;
    h256 blockHash;

    virtual void streamRLPFields(RLPStream& _s) const { _s << idx << term << height << blockHash; }
    virtual void populate(RLP const& _rlp)
    {
        int field = 0;
        try
        {
            idx = _rlp[field = 0].toInt<u256>();
            term = _rlp[field = 1].toInt<u256>();
            height = _rlp[field = 2].toInt<u256>();
            blockHash = _rlp[field = 3].toHash<h256>(RLP::VeryStrict);
        }
        catch (Exception const& _e)
        {
            RAFT_LOG(DEBUG) << "[#populate] invalid msg format, [field]=" << field;

            _e << dev::eth::errinfo_name("invalid msg format")
               << dev::eth::BadFieldError(field, toHex(_rlp[field].data().toBytes()));
            throw;
        }
    }
};

struct RaftVoteReq : public RaftMsg
{
    u256 candidate;
    u256 lastLeaderTerm;

    virtual void streamRLPFields(RLPStream& _s) const
    {
        RaftMsg::streamRLPFields(_s);
        _s << candidate << lastLeaderTerm;
    }

    virtual void populate(RLP const& _rlp)
    {
        RaftMsg::populate(_rlp);
        int field = 0;
        try
        {
            candidate = _rlp[field = 4].toInt<u256>();
            lastLeaderTerm = _rlp[field = 5].toInt<u256>();
        }
        catch (Exception const& _e)
        {
            RAFT_LOG(DEBUG) << "[#populate] invalid msg format, [field]=" << field;

            _e << dev::eth::errinfo_name("invalid msg format")
               << dev::eth::BadFieldError(field, toHex(_rlp[field].data().toBytes()));
            throw;
        }
    }
};

struct VoteState
{
    u256 vote = 0;
    u256 unVote = 0;
    u256 lastTermErr = 0;
    u256 firstVote = 0;
    u256 discardedVote = 0;

    u256 totalVoteCount() { return vote + unVote + lastTermErr + firstVote + discardedVote; }
};

struct RaftVoteResp : public RaftMsg
{
    u256 voteFlag;
    u256 lastLeaderTerm;

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
            voteFlag = _rlp[field = 4].toInt<u256>();
            lastLeaderTerm = _rlp[field = 5].toInt<u256>();
        }
        catch (Exception const& _e)
        {
            RAFT_LOG(DEBUG) << "[#populate] invalid msg format, [field]=" << field;

            _e << dev::eth::errinfo_name("invalid msg format")
               << dev::eth::BadFieldError(field, toHex(_rlp[field].data().toBytes()));
            throw;
        }
    }
};

struct RaftHeartBeat : public RaftMsg
{
    u256 leader;

    virtual void streamRLPFields(RLPStream& _s) const
    {
        RaftMsg::streamRLPFields(_s);
        _s << leader;
    }

    virtual void populate(RLP const& _rlp)
    {
        RaftMsg::populate(_rlp);
        int field = 0;
        try
        {
            leader = _rlp[field = 4].toInt<u256>();
        }
        catch (Exception const& _e)
        {
            RAFT_LOG(DEBUG) << "[#populate] invalid msg format, [field]=" << field;

            _e << dev::eth::errinfo_name("invalid msg format")
               << dev::eth::BadFieldError(field, toHex(_rlp[field].data().toBytes()));
            throw;
        }
    }
};

struct RaftHeartBeatResp : public RaftMsg
{
};
}  // namespace consensus
}  // namespace dev
