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
/** @file BlockHeader.h
 * @author Gav Wood <i@gavwood.com>
 * @date 2014
 *
 * @author wheatli
 * @date 2018.8.28
 * @brief Block header structures of FISCO-BCOS
 */

#pragma once

#include "Common.h"
#include "Exceptions.h"
#include <libdevcore/Common.h>
#include <libdevcore/Guards.h>
#include <libdevcore/RLP.h>
#include <libdevcrypto/Hash.h>
#include <algorithm>

namespace dev
{
namespace eth
{
enum Strictness
{
    CheckEverything,
    QuickNonce,
    CheckNothingNew
};

enum BlockDataType
{
    HeaderData,
    BlockData
};

/** @brief Encapsulation of a block header.
 * Class to contain all of a block header's data. It is able to parse a block header and populate
 * from some given RLP block serialisation with the static fromHeader(), through the method
 * populate(). This will not conduct any verification above basic formating. In this case extra
 * verification can be performed through verify().
 *
 * The object may also be populated from an entire block through the explicit
 * constructor BlockHeader(bytesConstRef) and manually with the populate() method. These will
 * conduct verification of the header against the other information in the block.
 *
 * The object may be populated with a template given a parent BlockHeader object with the
 * populateFromParent() method. The genesis block info may be retrieved with genesis() and the
 * corresponding RLP block created with createGenesisBlock().
 *
 * To determine the header hash without the nonce (for sealing), the method hash(WithoutNonce) is
 * provided.
 *
 * The default constructor creates an empty object, which can be tested against with the boolean
 * conversion operator.
 */
class BlockHeader
{
    friend class BlockChain;

public:
    const BlockNumber MaxBlockNumber = INT64_MAX;
    const unsigned BasicFields = 13;
    BlockHeader();
    /// construct block or block header according to input params
    explicit BlockHeader(
        bytesConstRef _data, BlockDataType _bdt = BlockData, h256 const& _hashWith = h256());
    explicit BlockHeader(
        bytes const& _data, BlockDataType _bdt = BlockData, h256 const& _hashWith = h256())
      : BlockHeader(&_data, _bdt, _hashWith)
    {}
    BlockHeader(BlockHeader const& _other);

    /// assignment function
    BlockHeader& operator=(BlockHeader const& _other);

    /// extract block header from block
    static RLP extractHeader(bytesConstRef _block);
    static RLP extractBlock(bytesConstRef _block);

    /// get the hash of block header according to block data
    static h256 headerHashFromBlock(bytes const& _block) { return headerHashFromBlock(&_block); }
    static h256 headerHashFromBlock(bytesConstRef _block);


    /// operator override: (), ==, !=
    explicit operator bool() const { return m_timestamp != UINT64_MAX; }
    bool operator==(BlockHeader const& _cmp) const { return hash() == _cmp.hash(); }
    bool operator!=(BlockHeader const& _cmp) const { return !operator==(_cmp); }

    /// populate block header from parent
    void populateFromParent(BlockHeader const& parent);
    void encode(bytes& _header) const;
    void decode(bytesConstRef& _header_data);
    void clear();

    /// block header verify
    // TODO: pull out into abstract class Verifier.
    void verify(Strictness _s = CheckEverything, BlockHeader const& _parent = BlockHeader(),
        bytesConstRef _block = bytesConstRef()) const;
    void verify(Strictness _s, bytesConstRef _block) const { verify(_s, BlockHeader(), _block); }

    ///------set interfaces related to block header------
    ///
    h256 hash() const;
    void noteDirty() const
    {
        Guard l(m_hashLock);
        m_hash = h256();
    }
    /// field 0: set parent
    void setParentHash(h256 const& _parentHash)
    {
        m_parentHash = _parentHash;
        noteDirty();
    }
    /// field 1-3: set roots:
    /// param1: transaction root
    /// param2: receipt root
    /// param3: state root
    void setRoots(h256 const& _trans_root, h256 const& _receipt_root, h256 const& _state_root)
    {
        m_transactionsRoot = _trans_root;
        m_receiptsRoot = _receipt_root;
        m_stateRoot = _state_root;
        noteDirty();
    }

    void setDBhash(h256 const& _dbHash)
    {
        m_dbHash = _dbHash;
        noteDirty();
    }
    /// field 5: set logBloom
    void setLogBloom(LogBloom const& _logBloom)
    {
        m_logBloom = _logBloom;
        noteDirty();
    }
    /// field 6: set block number
    void setNumber(int64_t _blockNumber)
    {
        m_number = _blockNumber;
        noteDirty();
    }
    /// field 7: set gas limit
    void setGasLimit(u256 const& _gasLimit)
    {
        m_gasLimit = _gasLimit;
        noteDirty();
    }
    /// field 8: set gas used
    void setGasUsed(u256 const& _gasUsed)
    {
        m_gasUsed = _gasUsed;
        noteDirty();
    }
    /// field 9: set/append extra data
    void appendExtraDataArray(bytes const& _content)
    {
        m_extraData.push_back(_content);
        noteDirty();
    }

    bool appendExtraDataItem(bytes const& _content, unsigned int _index = 0)
    {
        if (_index >= m_extraData.size())
        {
            return false;
        }
        else
        {
            m_extraData[_index] = _content;
            noteDirty();
            return true;
        }
    }

    bool setExtraData(bytes const& _content, unsigned int _index = 0)
    {
        if (_index >= m_extraData.size())
            return false;
        else
        {
            m_extraData[_index] = _content;
            noteDirty();
            return true;
        }
    }

    void setExtraData(std::vector<bytes> const& _extra)
    {
        m_extraData = _extra;
        noteDirty();
    }
    /// field 10: set timestamp
    void setTimestamp(uint64_t _timestamp)
    {
        m_timestamp = _timestamp;
        noteDirty();
    }
    /// field 11: set m_sealer
    void setSealer(u256 _sealer)
    {
        m_sealer = _sealer;
        noteDirty();
    }
    /// filed 12: set m_sealerList
    void setSealerList(h512s const& _sealerList)
    {
        m_sealerList = _sealerList;
        noteDirty();
    }
    ///------set interfaces related to block header END------

    /// ------ get interfaces related to block header------
    h256 const& parentHash() const { return m_parentHash; }  /// field 0
    h256 const& stateRoot() const { return m_stateRoot; }
    void setStateRoot(h256 const& _stateRoot)  /// field 1
    {
        m_stateRoot = _stateRoot;
        noteDirty();
    }
    h256 const& transactionsRoot() const { return m_transactionsRoot; }  /// field 2
    void setTransactionsRoot(h256 const& _transactionsRoot)
    {
        m_transactionsRoot = _transactionsRoot;
        noteDirty();
    }
    h256 const& receiptsRoot() const { return m_receiptsRoot; }  /// field 3
    void setReceiptsRoot(h256 const& _receiptsRoot)
    {
        m_receiptsRoot = _receiptsRoot;
        noteDirty();
    }
    h256 const& dbHash() const { return m_dbHash; }          /// field 4
    LogBloom const& logBloom() const { return m_logBloom; }  /// field 5
    int64_t number() const { return m_number; }              /// field 6
    u256 const& gasLimit() const { return m_gasLimit; }      /// field 7
    u256 const& gasUsed() const { return m_gasUsed; }        /// field 8
    uint64_t timestamp() const { return m_timestamp; }       /// field 9
    bytes const& extraData(unsigned int _index) const { return m_extraData[_index]; }

    std::vector<bytes> const& extraData() const { return m_extraData; }  /// field 10
    u256 const& sealer() const { return m_sealer; }                      /// field 11
    h512s const& sealerList() const { return m_sealerList; }             /// field 12
    /// ------ get interfaces related to block header END------
    void populate(RLP const& _header);

private:  /// private function fileds
    /// trans all fileds of the block header into a given stream
    void streamRLPFields(RLPStream& _s) const;
    h256 hashRawRead() const
    {
        Guard l(m_hashLock);
        return m_hash;
    }

private:  /// private data fields
    /// -------structure of block header------
    h256 m_parentHash;
    mutable h256 m_stateRoot;  // from state
    mutable h256 m_transactionsRoot;
    mutable h256 m_receiptsRoot;
    h256 m_dbHash;  // from MemoryTableFactory
    LogBloom m_logBloom;
    int64_t m_number = 0;
    u256 m_gasLimit;
    u256 m_gasUsed;
    uint64_t m_timestamp = UINT64_MAX;
    std::vector<bytes> m_extraData;  /// field for extension
    /// Extended fields of FISCO-BCOS
    u256 m_sealer = Invalid256;  /// index of the sealer created this block
    h512s m_sealerList;          /// sealer list
    /// -------structure of block header end------

    mutable h256 m_hash;       ///< (Memoised) hash of the block header with seal.
    mutable Mutex m_hashLock;  ///< A lock for both m_hash
};

inline std::ostream& operator<<(std::ostream& _out, BlockHeader const& _bi)
{
    _out << _bi.hash() << " " << _bi.parentHash() << " "
         << " " << _bi.stateRoot() << " " << _bi.transactionsRoot() << " " << _bi.receiptsRoot()
         << " " << _bi.dbHash() << " " << _bi.logBloom() << " " << _bi.number() << " "
         << _bi.gasLimit() << " " << _bi.gasUsed() << " " << _bi.timestamp() << " " << _bi.sealer();
    return _out;
}

}  // namespace eth
}  // namespace dev
