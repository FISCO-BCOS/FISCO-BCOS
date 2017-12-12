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

#include <libdevcore/concurrent_queue.h>
#include <libdevcore/easylog.h>
#include <libdevcore/RLP.h>
#include <libdevcrypto/Common.h>
#include <libethcore/Exceptions.h>

namespace dev
{

namespace eth
{

enum RaftPacketType : byte
{
	RaftVoteReqPacket = 0x00,
	RaftVoteRespPacket = 0x01,
	RaftHeartBeatPacket = 0x02,
	RaftHeartBeatRespPacket = 0x03,

	RaftPacketCount
};

enum RaftRole {
	EN_STATE_LEADER = 1,
	EN_STATE_FOLLOWER = 2,
	EN_STATE_CANDIDATE = 3
};

enum VoteRespFlag {
	VOTE_RESP_REJECT = 0,
	VOTE_RESP_LEADER_REJECT = 1,
	VOTE_RESP_LASTTERM_ERROR = 2,
	VOTE_RESP_FIRST_VOTE = 3,
	VOTE_RESP_DISCARD = 4,
	VOTE_RESP_GRANTED = 5
};

enum HandleVoteResult {
	TO_LEADER,
	TO_FOLLOWER,
	NONE
};

struct RaftMsgPacket {
	u256 node_idx;
	h512 node_id;
	unsigned packet_id;
	bytes data; // rlp data

	RaftMsgPacket(): node_idx(h256(0)), node_id(h512(0)), packet_id(0) {}
	RaftMsgPacket(u256 _idx, h512 _id, unsigned _pid, bytesConstRef _data)
		: node_idx(_idx), node_id(_id), packet_id(_pid), data(_data.toBytes()) {}
};
using RaftMsgQueue = dev::concurrent_queue<RaftMsgPacket>;

struct RaftMsg {
	u256 idx;
	u256 term;
	u256 height;
	h256 block_hash;

	virtual void streamRLPFields(RLPStream& _s) const {
		_s << idx << term << height << block_hash;
	}
	virtual void  populate(RLP const& _rlp) {
		int field = 0;
		try	{
			idx = _rlp[field = 0].toInt<u256>();
			term = _rlp[field = 1].toInt<u256>();
			height = _rlp[field = 2].toInt<u256>();
			block_hash = _rlp[field = 3].toHash<h256>(RLP::VeryStrict);
		} catch (Exception const& _e)	{
			_e << errinfo_name("invalid msg format") << BadFieldError(field, toHex(_rlp[field].data().toBytes()));
			throw;
		}
	}
};

struct RaftVoteReq : public RaftMsg {
	u256 candidate;
	u256 last_leader_term;

	virtual void streamRLPFields(RLPStream& _s) const {
		RaftMsg::streamRLPFields(_s);
		_s << candidate << last_leader_term;
	}
	virtual void  populate(RLP const& _rlp) {
		RaftMsg::populate(_rlp);
		int field = 0;
		try	{
			candidate = _rlp[field = 4].toInt<u256>();
			last_leader_term = _rlp[field = 5].toInt<u256>();
		} catch (Exception const& _e)	{
			_e << errinfo_name("invalid msg format") << BadFieldError(field, toHex(_rlp[field].data().toBytes()));
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

	u256 totalVoteCount() {
		return vote + unVote + lastTermErr + firstVote + discardedVote;
	}
};

struct RaftVoteResp : public RaftMsg {
	u256 vote_flag;
	u256 last_leader_term;

	virtual void streamRLPFields(RLPStream& _s) const {
		RaftMsg::streamRLPFields(_s);
		_s << vote_flag << last_leader_term;
	}
	virtual void  populate(RLP const& _rlp) {
		RaftMsg::populate(_rlp);
		int field = 0;
		try	{
			vote_flag = _rlp[field = 4].toInt<u256>();
			last_leader_term = _rlp[field = 5].toInt<u256>();
		} catch (Exception const& _e)	{
			_e << errinfo_name("invalid msg format") << BadFieldError(field, toHex(_rlp[field].data().toBytes()));
			throw;
		}
	}
};

struct RaftHeartBeat : public RaftMsg {
	u256 leader;

	virtual void streamRLPFields(RLPStream& _s) const {
		RaftMsg::streamRLPFields(_s);
		_s << leader;
	}
	virtual void  populate(RLP const& _rlp) {
		RaftMsg::populate(_rlp);
		int field = 0;
		try	{
			leader = _rlp[field = 4].toInt<u256>();
		} catch (Exception const& _e)	{
			_e << errinfo_name("invalid msg format") << BadFieldError(field, toHex(_rlp[field].data().toBytes()));
			throw;
		}
	}
};

struct RaftHeartBeatResp : public RaftMsg  {};


}
}
