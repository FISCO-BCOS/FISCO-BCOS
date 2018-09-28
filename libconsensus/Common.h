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
#include <libdevcore/FixedHash.h>
#include <libdevcore/RLP.h>
#include <libdevcore/SHA3.h>
#include <libdevcore/easylog.h>
#include <libdevcrypto/Common.h>
#include <libethcore/Exceptions.h>
namespace dev
{
namespace consensus
{
enum NodeAccountType
{
    NonMinerAccount = 0,
    MinerAccount
};
struct ConsensusStatus
{
    std::string consensusEngine;
    dev::h512s minerList;
};

// for pbft
struct PBFTMsg
{
    u256 height = Invalid256;
    u256 view = Invalid256;
    u256 idx = Invalid256;
    u256 timestamp = Invalid256;
    h256 block_hash;
    Signature sig;   // signature of block_hash
    Signature sig2;  // other fileds' signature

    virtual void encode(bytes& encodedBytes) const
    {
        RLPStream tmp;
        streamRLPFields(tmp);
        tmp.swapOut(encodedBytes);
    }

    virtual void decode(bytesConstRef data)
    {
        RLP rlp(data);
        populate(rlp);
    }

    void streamRLPFields(RLPStream& _s) const
    {
        _s << height << view << idx << timestamp << block_hash << sig.asBytes() << sig2.asBytes();
    }

    void populate(RLP const& _rlp)
    {
        int field = 0;
        try
        {
            height = _rlp[field = 0].toInt<u256>();
            view = _rlp[field = 1].toInt<u256>();
            idx = _rlp[field = 2].toInt<u256>();
            timestamp = _rlp[field = 3].toInt<u256>();
            block_hash = _rlp[field = 4].toHash<h256>(RLP::VeryStrict);
            sig = dev::Signature(_rlp[field = 5].toBytesConstRef());
            sig2 = dev::Signature(_rlp[field = 6].toBytesConstRef());
        }
        catch (Exception const& _e)
        {
            _e << dev::eth::errinfo_name("invalid msg format")
               << dev::eth::BadFieldError(field, toHex(_rlp[field].data().toBytes()));
            throw;
        }
    }

    void clear()
    {
        height = Invalid256;
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
};
struct CommitReq : public PBFTMsg
{
};
struct ViewChangeReq : public PBFTMsg
{
};
}  // namespace consensus
}  // namespace dev
