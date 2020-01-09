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
#include <libconsensus/Common.h>
#include <libdevcore/RLP.h>
#include <libdevcrypto/Common.h>
#include <libdevcrypto/Hash.h>
#include <libethcore/Block.h>
#include <libethcore/Exceptions.h>

#define PBFTENGINE_LOG(LEVEL) LOG(LEVEL) << LOG_BADGE("CONSENSUS") << LOG_BADGE("PBFT")
#define PBFTSEALER_LOG(LEVEL) LOG(LEVEL) << LOG_BADGE("CONSENSUS") << LOG_BADGE("SEALER")
#define PBFTReqCache_LOG(LEVEL) LOG(LEVEL) << LOG_BADGE("CONSENSUS")
namespace dev
{
namespace consensus
{
// for bip152: packetType for partiallyBlock
enum P2PPacketType : uint32_t
{
    // PartiallyPreparePacket
    // note: the forwarded prepare messages include all the transaction data
    PartiallyPreparePacket = 0x1,
    // represent that the node should response the missed transaction data
    GetMissedTxsPacket = 0x2,
    // represent that the node receives the missed transaction data
    MissedTxsPacket = 0x3,
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

/// PBFT message
struct PBFTMsgPacket
{
    /// the index of the node that sends this pbft message
    IDXTYPE node_idx;
    /// the node id of the node that sends this pbft message
    h512 node_id;
    /// type of the packet(maybe prepare, sign or commit)
    /// (receive from the network or send to the network)
    byte packet_id;
    /// ttl
    uint8_t ttl;
    /// the data of concrete request(receive from or send to the network)
    bytes data;
    /// timestamp of receive this pbft message
    u256 timestamp;
    /// endpoint
    std::string endpoint;
    // the node that disconnected from this node, but the packet should reach
    std::shared_ptr<dev::h512s> forwardNodes;

    using Ptr = std::shared_ptr<PBFTMsgPacket>;

    /// default constructor
    PBFTMsgPacket()
      : node_idx(0),
        node_id(h512(0)),
        packet_id(0),
        ttl(MAXTTL),
        timestamp(u256(utcTime())),
        forwardNodes(nullptr)
    {}

    virtual ~PBFTMsgPacket() = default;

    void setForwardNodes(std::shared_ptr<dev::h512s> _forwardNodes)
    {
        forwardNodes = _forwardNodes;
    }

    bool operator==(PBFTMsgPacket const& msg)
    {
        return node_idx == msg.node_idx && node_id == msg.node_id && packet_id == msg.packet_id &&
               data == msg.data;
    }
    bool operator!=(PBFTMsgPacket const& msg) { return !operator==(msg); }
    /**
     * @brief : encode network-send part of PBFTMsgPacket into bytes (RLP encoder)
     * @param encodedBytes: encoded bytes of the network-send part of PBFTMsgPacket
     */
    virtual void encode(bytes& encodedBytes) const
    {
        RLPStream tmp;
        streamRLPFields(tmp);
        RLPStream list_rlp;
        list_rlp.appendList(1).append(tmp.out());
        list_rlp.swapOut(encodedBytes);
    }
    /**
     * @brief : decode the network-receive part of PBFTMsgPacket into PBFTMsgPacket object
     * @param data: network-receive part of PBFTMsgPacket
     * @ Exception Case: if decode failed, we throw exceptions
     */
    virtual void decode(bytesConstRef data)
    {
        RLP rlp(data);
        populate(rlp[0]);
    }

    /// RLP decode: serialize network-received packet-data from bytes to RLP
    virtual void streamRLPFields(RLPStream& s) const { s << packet_id << ttl << data; }

    /**
     * @brief: set non-network-receive-or-send part of PBFTMsgPacket
     * @param idx: the index of the node that send the PBFTMsgPacket
     * @param nodeId : the id of the node that send the PBFTMsgPacket
     */
    void setOtherField(IDXTYPE const& idx, h512 const& nodeId, std::string const& _endpoint)
    {
        node_idx = idx;
        node_id = nodeId;
        endpoint = _endpoint;
        timestamp = u256(utcTime());
    }
    /// populate PBFTMsgPacket from RLP object
    virtual void populate(RLP const& rlp)
    {
        try
        {
            int field = 0;
            packet_id = rlp[field = 0].toInt<uint8_t>();
            ttl = rlp[field = 1].toInt<uint8_t>();
            data = rlp[field = 2].toBytes();
        }
        catch (Exception const& e)
        {
            e << dev::eth::errinfo_name("invalid msg format");
            throw;
        }
    }
};

// PBFTMsgPacket for ttl-optimize
class OPBFTMsgPacket : public PBFTMsgPacket
{
public:
    using Ptr = std::shared_ptr<OPBFTMsgPacket>;
    OPBFTMsgPacket() : PBFTMsgPacket()
    {
        // the node that disconnected from this node, but the packet should reach
        forwardNodes = std::make_shared<dev::h512s>();
    }

    void streamRLPFields(RLPStream& _s) const override
    {
        PBFTMsgPacket::streamRLPFields(_s);
        _s.appendVector(*forwardNodes);
    }

    void populate(RLP const& _rlp) override
    {
        try
        {
            PBFTMsgPacket::populate(_rlp);
            *forwardNodes = _rlp[3].toVector<dev::h512>();
        }
        catch (Exception const& e)
        {
            e << dev::eth::errinfo_name("invalid msg format");
            throw;
        }
    }
};

/// the base class of PBFT message
struct PBFTMsg
{
    /// the number of the block that is handling
    int64_t height = -1;
    /// view when construct this PBFTMsg
    VIEWTYPE view = MAXVIEW;
    /// index of the node generate the PBFTMsg
    IDXTYPE idx = MAXIDX;
    /// timestamp when generate the PBFTMsg
    u256 timestamp = Invalid256;
    /// block-header hash of the block handling
    h256 block_hash = h256();
    /// signature to the block_hash
    Signature sig = Signature();
    /// signature to the hash of other fields except block_hash, sig and sig2
    Signature sig2 = Signature();
    PBFTMsg() = default;
    PBFTMsg(KeyPair const& _keyPair, int64_t const& _height, VIEWTYPE const& _view,
        IDXTYPE const& _idx, h256 const _blockHash)
    {
        height = _height;
        view = _view;
        idx = _idx;
        timestamp = u256(utcTime());
        block_hash = _blockHash;
        sig = signHash(block_hash, _keyPair);
        sig2 = signHash(fieldsWithoutBlock(), _keyPair);
    }
    virtual ~PBFTMsg() = default;

    bool operator==(PBFTMsg const& req) const
    {
        return height == req.height && view == req.view && block_hash == req.block_hash &&
               sig == req.sig && sig2 == req.sig2;
    }

    bool operator!=(PBFTMsg const& req) const { return !operator==(req); }
    /**
     * @brief: encode the PBFTMsg into bytes
     * @param encodedBytes: the encoded bytes of specified PBFTMsg
     */
    virtual void encode(bytes& encodedBytes) const
    {
        RLPStream tmp;
        streamRLPFields(tmp);
        RLPStream list_rlp;
        list_rlp.appendList(1).append(tmp.out());
        list_rlp.swapOut(encodedBytes);
    }

    /**
     * @brief : decode the bytes received from network into PBFTMsg object
     * @param data : network-received data to be decoded
     * @param index: the index of RLP data need to be populated
     * @Exception Case: if decode failed, throw exception directly
     */
    virtual void decode(bytesConstRef data, size_t const& index = 0)
    {
        RLP rlp(data);
        populate(rlp[index]);
    }

    /// trans PBFTMsg into RLPStream for encoding
    virtual void streamRLPFields(RLPStream& _s) const
    {
        _s << height << view << idx << timestamp << block_hash << sig.asBytes() << sig2.asBytes();
    }

    /// populate specified rlp into PBFTMsg object
    virtual void populate(RLP const& rlp)
    {
        int field = 0;
        try
        {
            height = rlp[field = 0].toInt<int64_t>();
            view = rlp[field = 1].toInt<VIEWTYPE>();
            idx = rlp[field = 2].toInt<IDXTYPE>();
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

    /// clear the PBFTMsg
    void clear()
    {
        height = -1;
        view = MAXVIEW;
        idx = MAXIDX;
        timestamp = Invalid256;
        block_hash = h256();
        sig = Signature();
        sig2 = Signature();
    }

    /// get the hash of the fields without block_hash, sig and sig2
    h256 fieldsWithoutBlock() const
    {
        RLPStream ts;
        ts << height << view << idx << timestamp;
        return dev::sha3(ts.out());
    }

    /**
     * @brief : sign for specified hash using given keyPair
     * @param hash: hash data need to be signed
     * @param keyPair: keypair used to sign for the specified hash
     * @return Signature: signature result
     */
    Signature signHash(h256 const& hash, KeyPair const& keyPair) const
    {
        return dev::sign(keyPair.secret(), hash);
    }

    std::string uniqueKey() const { return sig.hex() + sig2.hex(); }
};

/// definition of the prepare requests
struct PrepareReq : public PBFTMsg
{
    using Ptr = std::shared_ptr<PrepareReq>;
    /// block data
    std::shared_ptr<bytes> block;
    std::shared_ptr<dev::eth::Block> pBlock = nullptr;
    /// execution result of block(save the execution result temporarily)
    /// no need to send or receive accross the network
    dev::blockverifier::ExecutiveContext::Ptr p_execContext = nullptr;

    /// default constructor
    PrepareReq() { block = std::make_shared<dev::bytes>(); }
    virtual ~PrepareReq() {}
    PrepareReq(KeyPair const& _keyPair, int64_t const& _height, VIEWTYPE const& _view,
        IDXTYPE const& _idx, h256 const _blockHash)
      : PBFTMsg(_keyPair, _height, _view, _idx, _blockHash), p_execContext(nullptr)
    {
        block = std::make_shared<dev::bytes>();
    }

    /**
     * @brief: populate the prepare request from specified prepare request,
     *         given view and node index
     *
     * @param req: given prepare request to populate the PrepareReq object
     * @param keyPair: keypair used to sign for the PrepareReq
     * @param _view: current view
     * @param _idx: index of the node that generates this PrepareReq
     */
    PrepareReq(
        PrepareReq const& req, KeyPair const& keyPair, VIEWTYPE const& _view, IDXTYPE const& _idx)
    {
        block = std::make_shared<dev::bytes>();
        height = req.height;
        view = _view;
        idx = _idx;
        timestamp = u256(utcTime());
        block_hash = req.block_hash;
        sig = signHash(block_hash, keyPair);
        sig2 = signHash(fieldsWithoutBlock(), keyPair);
        block = req.block;
        pBlock = req.pBlock;
        p_execContext = nullptr;
    }

    /**
     * @brief: construct PrepareReq from given block, view and node idx
     * @param blockStruct : the given block used to populate the PrepareReq
     * @param keyPair : keypair used to sign for the PrepareReq
     * @param _view : current view
     * @param _idx : index of the node that generates this PrepareReq
     */
    PrepareReq(dev::eth::Block::Ptr blockStruct, KeyPair const& keyPair, VIEWTYPE const& _view,
        IDXTYPE const& _idx, bool const& _onlyHash = false)
    {
        block = std::make_shared<dev::bytes>();
        height = blockStruct->blockHeader().number();
        view = _view;
        idx = _idx;
        timestamp = u256(utcTime());
        block_hash = blockStruct->blockHeader().hash();
        sig = signHash(block_hash, keyPair);
        sig2 = signHash(fieldsWithoutBlock(), keyPair);
        blockStruct->encodeProposal(block, _onlyHash);
        pBlock = blockStruct;
        p_execContext = nullptr;
    }

    /**
     * @brief : update the PrepareReq with specified block and block-execution-result
     *
     * @param sealing : object contains both block and block-execution-result
     * @param keyPair : keypair used to sign for the PrepareReq
     */
    PrepareReq(PrepareReq const& req, Sealing const& sealing, KeyPair const& keyPair)
    {
        block = std::make_shared<dev::bytes>();
        height = req.height;
        view = req.view;
        idx = req.idx;
        p_execContext = sealing.p_execContext;
        /// sealing.block.encode(block);
        timestamp = u256(utcTime());
        block_hash = sealing.block->blockHeader().hash();
        sig = signHash(block_hash, keyPair);
        sig2 = signHash(fieldsWithoutBlock(), keyPair);
        pBlock = sealing.block;
        LOG(DEBUG) << "Re-generate prepare_requests since block has been executed, time = "
                   << timestamp << " , block_hash: " << block_hash.abridged();
    }

    bool operator==(PrepareReq const& req) const
    {
        return PBFTMsg::operator==(req) && *req.block == *block;
    }
    bool operator!=(PrepareReq const& req) const { return !(operator==(req)); }

    /// trans PrepareReq from object to RLPStream
    virtual void streamRLPFields(RLPStream& _s) const
    {
        PBFTMsg::streamRLPFields(_s);
        _s << *block;
    }

    /// populate PrepareReq from given RLP object
    virtual void populate(RLP const& _rlp)
    {
        PBFTMsg::populate(_rlp);
        int field = 0;
        try
        {
            *block = _rlp[field = 7].toBytes();
        }
        catch (Exception const& _e)
        {
            _e << dev::eth::errinfo_name("invalid msg format")
               << dev::eth::BadFieldError(field, toHex(_rlp[field].data().toBytes()));
            throw;
        }
    }
};

/// signature request
struct SignReq : public PBFTMsg
{
    using Ptr = std::shared_ptr<SignReq>;
    SignReq() = default;

    /**
     * @brief: populate the SignReq from given PrepareReq and node index
     *
     * @param req: PrepareReq used to populate the SignReq
     * @param keyPair: keypair used to sign for the SignReq
     * @param _idx: index of the node that generates this SignReq
     */
    SignReq(PrepareReq const& req, KeyPair const& keyPair, IDXTYPE const& _idx)
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

/// commit request
struct CommitReq : public PBFTMsg
{
    using Ptr = std::shared_ptr<CommitReq>;
    CommitReq() = default;
    /**
     * @brief: populate the CommitReq from given PrepareReq and node index
     *
     * @param req: PrepareReq used to populate the CommitReq
     * @param keyPair: keypair used to sign for the CommitReq
     * @param _idx: index of the node that generates this CommitReq
     */
    CommitReq(PrepareReq const& req, KeyPair const& keyPair, IDXTYPE const& _idx)
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

/// view change request
struct ViewChangeReq : public PBFTMsg
{
    using Ptr = std::shared_ptr<ViewChangeReq>;
    ViewChangeReq() = default;
    /**
     * @brief: generate ViewChangeReq from given params
     * @param keyPair: keypair used to sign for the ViewChangeReq
     * @param _height: current block number
     * @param _view: current view
     * @param _idx: index of the node that generates this ViewChangeReq
     * @param _hash: block hash
     */
    ViewChangeReq(KeyPair const& keyPair, int64_t const& _height, VIEWTYPE const _view,
        IDXTYPE const& _idx, h256 const& _hash)
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
