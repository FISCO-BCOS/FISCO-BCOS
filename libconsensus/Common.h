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
 * @brief Common functions and types of Consensus module
 * @author: yujiechen
 * @date: 2018-09-21
 */
#pragma once
#include <libblockverifier/BlockVerifier.h>
#include <libdevcore/FixedHash.h>
#include <libdevcore/RLP.h>
#include <libdevcore/SHA3.h>
#include <libdevcore/easylog.h>
#include <libdevcrypto/Common.h>
#include <libethcore/Block.h>
#include <libethcore/Exceptions.h>
namespace dev
{
namespace consensus
{
DEV_SIMPLE_EXCEPTION(DisabledFutureTime);
DEV_SIMPLE_EXCEPTION(InvalidBlockHeight);
DEV_SIMPLE_EXCEPTION(ExistedBlock);
DEV_SIMPLE_EXCEPTION(ParentNoneExist);
DEV_SIMPLE_EXCEPTION(BlockMinerListWrong);
enum NodeAccountType
{
    ObserverAccount = 0,
    MinerAccount
};
struct ConsensusStatus
{
    std::string consensusEngine;
    dev::h512s minerList;
};
struct Sealing
{
    dev::eth::Block block;
    /// hash set for filter fetched transactions
    h256Hash m_transactionSet;
    dev::blockverifier::ExecutiveContext::Ptr p_execContext;
};

// for pbft
enum PBFTPacketType : byte
{
    PrepareReqPacket = 0x00,
    SignReqPacket = 0x01,
    CommitReqPacket = 0x02,
    ViewChangeReqPacket = 0x03,
    PBFTPacketCount
};
struct PBFTMsgPacket
{
    u256 node_idx;
    h512 node_id;
    unsigned packet_id;
    bytes data;  // rlp data
    u256 timestamp;
    PBFTMsgPacket() : node_idx(h256(0)), node_id(h512(0)), packet_id(0), timestamp(u256(utcTime()))
    {}
    inline void streamRLPFields(RLPStream& s) const { s << packet_id << data; }
    inline void setOtherField(u256 const& idx, h512 const& nodeId)
    {
        node_idx = idx;
        node_id = nodeId;
        timestamp = u256(utcTime());
    }

    virtual void encode(bytes& encodedBytes) const
    {
        RLPStream tmp;
        streamRLPFields(tmp);
        RLPStream list_rlp;
        list_rlp.appendList(1).append(tmp.out());
        list_rlp.swapOut(encodedBytes);
    }

    virtual void decode(bytesConstRef data)
    {
        RLP rlp(data);
        populate(rlp);
    }

    void populate(RLP const& rlp)
    {
        int field = 0;
        try
        {
            packet_id = rlp[field = 0].toInt<unsigned>();
            data = rlp[field = 1].data().toBytes();
        }
        catch (Exception const& e)
        {
            e << dev::eth::errinfo_name("invalid msg format")
              << dev::eth::BadFieldError(field, toHex(rlp[field].data().toBytes()));
            throw;
        }
    }
};

// for pbft
struct PBFTMsg
{
    int64_t height = -1;
    u256 view = Invalid256;
    u256 idx = Invalid256;
    u256 timestamp = Invalid256;
    h256 block_hash;
    Signature sig;   // signature of block_hash
    Signature sig2;  // other fileds' signature

    /// signature _hash with the private key of node
    Signature signHash(h256 const& hash, KeyPair const& keyPair) const
    {
        return dev::sign(keyPair.secret(), hash);
    }
    virtual void encode(bytes& encodedBytes) const
    {
        RLPStream tmp;
        streamRLPFields(tmp);
        RLPStream list_rlp;
        list_rlp.appendList(1).append(tmp.out());
        list_rlp.swapOut(encodedBytes);
    }

    virtual void decode(bytesConstRef data, size_t const& index = 0)
    {
        RLP rlp(data);
        populate(rlp[index]);
    }

    void streamRLPFields(RLPStream& _s) const
    {
        _s << height << view << idx << timestamp << block_hash << sig.asBytes() << sig2.asBytes();
    }

    void populate(RLP const& rlp)
    {
        int field = 0;
        try
        {
            height = rlp[field = 0].toInt<int64_t>();
            view = rlp[field = 1].toInt<u256>();
            idx = rlp[field = 2].toInt<u256>();
            timestamp = rlp[field = 3].toInt<u256>();
            block_hash = rlp[field = 4].toHash<h256>(RLP::VeryStrict);
            sig = dev::Signature(rlp[field = 5].toBytesConstRef());
            sig2 = dev::Signature(rlp[field = 6].toBytesConstRef());
        }
        catch (Exception const& _e)
        {
            _e << dev::eth::errinfo_name("invalid msg format")
               << dev::eth::BadFieldError(field, toHex(rlp[field].data().toBytes()));
            throw;
        }
    }

    void clear()
    {
        height = -1;
        view = Invalid256;
        idx = Invalid256;
        timestamp = Invalid256;
        block_hash = h256();
        sig = Signature();
        sig2 = Signature();
    }

    h256 fieldsWithoutBlock() const
    {
        RLPStream ts;
        ts << height << view << idx << timestamp;
        return dev::sha3(ts.out());
    }
};

struct PrepareReq : public PBFTMsg
{
    bytes block;
    dev::blockverifier::ExecutiveContext::Ptr p_execContext;

    PrepareReq() = default;
    PrepareReq(PrepareReq const& req, KeyPair const& keyPair, u256 const& _view, u256 const& _idx)
    {
        height = req.height;
        view = _view;
        idx = _idx;
        timestamp = u256(utcTime());
        block_hash = req.block_hash;
        sig = signHash(block_hash, keyPair);
        sig2 = signHash(fieldsWithoutBlock(), keyPair);
        block = req.block;
    }

    PrepareReq(
        dev::eth::Block& blockStruct, KeyPair const& keyPair, u256 const& _view, u256 const& _idx)
    {
        bytes block_data;
        blockStruct.encode(block_data);
        height = blockStruct.blockHeader().number();
        view = _view;
        idx = _idx;
        timestamp = u256(utcTime());
        block_hash = blockStruct.blockHeader().hash();
        sig = signHash(block_hash, keyPair);
        sig2 = signHash(fieldsWithoutBlock(), keyPair);
        block = block_data;
        p_execContext = nullptr;
    }

    void updatePrepareReq(Sealing& sealing, KeyPair const& keyPair)
    {
        timestamp = u256(utcTime());
        block_hash = sealing.block.blockHeader().hash();
        sig = signHash(block_hash, keyPair);
        sig2 = signHash(fieldsWithoutBlock(), keyPair);
        bytes block_data;
        sealing.block.encode(block_data);
        block = block_data;
        p_execContext = sealing.p_execContext;
        LOG(DEBUG) << "Re-generate prepare_requests since block has been executed, time = "
                   << timestamp;
    }

    virtual void encode(bytes& encodedBytes) const
    {
        RLPStream s;
        streamRLPFields(s);
        s.swapOut(encodedBytes);
    }
    virtual void decode(bytesConstRef data)
    {
        RLP rlp(data);
        populate(rlp);
    }

    void streamRLPFields(RLPStream& _s) const
    {
        PBFTMsg::streamRLPFields(_s);
        _s << block;
    }

    void populate(RLP const& _rlp)
    {
        PBFTMsg::populate(_rlp);
        int field = 0;
        try
        {
            block = _rlp[field = 7].toBytes();
        }
        catch (Exception const& _e)
        {
            _e << dev::eth::errinfo_name("invalid msg format")
               << dev::eth::BadFieldError(field, toHex(_rlp[field].data().toBytes()));
            throw;
        }
    }
};

struct SignReq : public PBFTMsg
{
    SignReq() = default;
    SignReq(PrepareReq const& req, KeyPair const& keyPair, u256 const& _idx)
    {
        height = req.height;
        view = req.view;
        idx = _idx;
        timestamp = u256(utcTime());
        block_hash = req.block_hash;
        sig = signHash(block_hash, keyPair);
        sig2 = signHash(fieldsWithoutBlock(), keyPair);
    }
};
struct CommitReq : public PBFTMsg
{
    CommitReq() = default;
    CommitReq(PrepareReq const& req, KeyPair const& keyPair, u256 const& _idx)
    {
        height = req.height;
        view = req.view;
        idx = _idx;
        timestamp = u256(utcTime());
        block_hash = req.block_hash;
        sig = signHash(block_hash, keyPair);
        sig2 = signHash(fieldsWithoutBlock(), keyPair);
    }
};
struct ViewChangeReq : public PBFTMsg
{
    ViewChangeReq() = default;
    ViewChangeReq(KeyPair const& keyPair, uint64_t const& _height, u256 const _view,
        u256 const& _idx, h256 const& _hash)
    {
        height = _height;
        view = _view;
        idx = _idx;
        timestamp = u256(utcTime());
        block_hash = _hash;
        sig = signHash(block_hash, keyPair);
        sig2 = signHash(fieldsWithoutBlock(), keyPair);
    }
};
}  // namespace consensus
}  // namespace dev
