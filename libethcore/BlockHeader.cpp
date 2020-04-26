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
/** @file BlockHeader.cpp
 * @author Gav Wood <i@gavwood.com>
 * @date 2014
 *
 * @author wheatli
 * @date 2018.8.28
 * @record change log to easylog
 */

#include "BlockHeader.h"
#include "Common.h"
#include "Exceptions.h"
#include <libdevcore/Common.h>
#include <libdevcore/MemoryDB.h>
#include <libdevcore/RLP.h>
#include <libdevcore/TrieDB.h>
#include <libdevcore/TrieHash.h>
using namespace std;
using namespace dev;
using namespace dev::eth;
/// construct empty block header
BlockHeader::BlockHeader() {}

/**
 * @brief: create block header according to given block header or block
 * @param _block: contents of block header or block
 * @param _bdt: data type---HeaderData or BlockData
 * @param _hashWith: if set, then assign 'm_hash' to '_hashWith'(given hash of block header)
 */
BlockHeader::BlockHeader(bytesConstRef _block, BlockDataType _bdt, h256 const& _hashWith)
{
    RLP header = _bdt == BlockData ? extractHeader(_block) : RLP(_block);
    m_hash = _hashWith ? _hashWith : crypto::Hash(header.data());
    populate(header);
}

/**
 * @brief: construct another block header
 * according to specified block header
 * @param _other: specified block header that has been regarded as a template
 */
BlockHeader::BlockHeader(BlockHeader const& _other)
  : m_parentHash(_other.parentHash()),
    m_stateRoot(_other.stateRoot()),
    m_transactionsRoot(_other.transactionsRoot()),
    m_receiptsRoot(_other.receiptsRoot()),
    m_dbHash(_other.dbHash()),
    m_logBloom(_other.logBloom()),
    m_number(_other.number()),
    m_gasLimit(_other.gasLimit()),
    m_gasUsed(_other.gasUsed()),
    m_timestamp(_other.timestamp()),
    m_extraData(_other.extraData()),
    m_sealer(_other.sealer()),
    m_sealerList(_other.sealerList()),
    m_hash(_other.hash())
{
    assert(*this == _other);
}

/// assignment override
BlockHeader& BlockHeader::operator=(BlockHeader const& _other)
{
    if (this == &_other)
        return *this;
    m_parentHash = _other.parentHash();
    m_stateRoot = _other.stateRoot();
    m_transactionsRoot = _other.transactionsRoot();
    m_receiptsRoot = _other.receiptsRoot();
    m_dbHash = _other.dbHash();
    m_logBloom = _other.logBloom();
    m_number = _other.number();
    m_gasLimit = _other.gasLimit();
    m_gasUsed = _other.gasUsed();
    m_timestamp = _other.timestamp();
    m_extraData = _other.extraData();
    m_sealer = _other.sealer();
    m_sealerList = _other.sealerList();
    // equal to m_hash of _other
    h256 hash = _other.hash();
    // set the real member of block header with lock protection
    {
        Guard l(m_hashLock);
        m_hash = std::move(hash);
    }
    // check
    assert(*this == _other);
    return *this;
}

/// clear the content of the block header
void BlockHeader::clear()
{
    m_parentHash = h256();
    m_stateRoot = EmptyTrie;
    m_transactionsRoot = EmptyTrie;
    m_receiptsRoot = EmptyTrie;
    m_dbHash = h256();
    m_logBloom = LogBloom();
    m_number = 0;
    m_gasLimit = 0;
    m_gasUsed = 0;
    m_timestamp = UINT64_MAX;
    m_extraData.clear();
    m_sealer = Invalid256;
    m_sealerList.clear();
    noteDirty();
}

/// get hash of the block header
h256 BlockHeader::hash() const
{
    Guard l(m_hashLock);
    if (m_hash == h256())
    {
        bytes header;
        encode(header);
        m_hash = crypto::Hash(header);
    }
    return m_hash;
}

void BlockHeader::encode(bytes& _header) const
{
    RLPStream _s;
    unsigned basicFieldsCnt = BasicFields;
    _s.appendList(basicFieldsCnt);
    BlockHeader::streamRLPFields(_s);
    _s.swapOut(_header);
}
void BlockHeader::streamRLPFields(RLPStream& _s) const
{
    _s << m_parentHash << m_stateRoot << m_transactionsRoot << m_receiptsRoot << m_dbHash
       << m_logBloom;
    _s.append(bigint(m_number));
    _s << m_gasLimit << m_gasUsed;
    _s.append(bigint(m_timestamp));
    _s.appendVector(m_extraData);
    _s << m_sealer;
    _s.appendVector(m_sealerList);
}

void BlockHeader::decode(bytesConstRef& _header_data)
{
    RLP header_rlp = RLP(_header_data);
    populate(header_rlp);
}

/// calculate hash of the block header according to given block
h256 BlockHeader::headerHashFromBlock(bytesConstRef _block)
{
    // TODO: exception unit test
    return crypto::Hash(RLP(_block)[0].data());
}

/**
 * @brief: extract block header from specified block,
 *         and return the rlp of the block header
 * @param _block : specified block need to extract block header
 * @return RLP : the RLP encode of the extracted block header
 */
RLP BlockHeader::extractHeader(bytesConstRef _block)
{
    RLP root = extractBlock(_block);
    return root[0];
}

RLP BlockHeader::extractBlock(bytesConstRef _block)
{
    RLP root(_block);
    if (!root.isList())
        BOOST_THROW_EXCEPTION(InvalidBlockFormat() << errinfo_comment("Block must be a list")
                                                   << BadFieldError(0, _block.toString()));
    // check block header(must be list)
    if (!root[0].isList())
        BOOST_THROW_EXCEPTION(InvalidBlockFormat() << errinfo_comment("Block header must be a list")
                                                   << BadFieldError(0, root[0].data().toString()));
    // check the transactions(must be list)
    /*if (!root[1].isList())
        BOOST_THROW_EXCEPTION(InvalidBlockFormat()
                              << errinfo_comment("Block transactions must be a list")
                              << BadFieldError(1, root[1].data().toString()));
                              */
    return root;
}


/**
 * @brief : create BlockHeader from given rlp
 * @param _header : the rlp code of the given block header
 */
void BlockHeader::populate(RLP const& _header)
{
    int field = 0;
    try
    {
        m_parentHash = _header[field = 0].toHash<h256>(RLP::VeryStrict);
        m_stateRoot = _header[field = 1].toHash<h256>(RLP::VeryStrict);
        m_transactionsRoot = _header[field = 2].toHash<h256>(RLP::VeryStrict);
        m_receiptsRoot = _header[field = 3].toHash<h256>(RLP::VeryStrict);
        m_dbHash = _header[field = 4].toHash<h256>(RLP::VeryStrict);
        m_logBloom = _header[field = 5].toHash<LogBloom>(RLP::VeryStrict);
        m_number = _header[field = 6].toPositiveInt64();
        m_gasLimit = _header[field = 7].toInt<u256>();
        m_gasUsed = _header[field = 8].toInt<u256>();
        m_timestamp = uint64_t(_header[field = 9]);
        m_extraData = _header[field = 10].toVector<bytes>();
        m_sealer = _header[field = 11].toInt<u256>();
        m_sealerList = _header[field = 12].toVector<h512>();
    }
    catch (Exception const& _e)
    {
        _e << errinfo_name("invalid block header format")
           << BadFieldError(field, toHex(_header[field].data().toBytes()));
        throw;
    }
}


/// create block header through parent block header
void BlockHeader::populateFromParent(BlockHeader const& _parent)
{
    m_stateRoot = _parent.stateRoot();
    m_number = _parent.number() + 1;
    m_parentHash = _parent.hash();
    m_gasLimit = _parent.m_gasLimit;
    m_gasUsed = u256(0);
    m_sealer = Invalid256;
    noteDirty();
}

/**
 * @brief : check the validation of the block header
 *
 * @param _s: strictness, including CheckEverything,QuickNonce and CheckNothingNew
 * @param _parent: the parent block header, default is a empty block header
 *                 (if the parent block header is empty, doesn't need to be verified)
 *                 (if the parent block header is not empty, must chcek hash, timestamp and
 * blocknumber)
 * @param _block: the block contains the block header to be verified
 *                default is empty, doesn't need to be verified
 *                if the block is not empty, must check the transacton state
 */
void BlockHeader::verify(Strictness _s, BlockHeader const& _parent, bytesConstRef _block) const
{
    /// check block number
    if (m_number >= MaxBlockNumber || m_number < 0)
    {
        BOOST_THROW_EXCEPTION(InvalidNumber());
    }
    /// check gas
    if (_s != CheckNothingNew && m_gasUsed > m_gasLimit)
    {
        BOOST_THROW_EXCEPTION(
            TooMuchGasUsed() << RequirementError(bigint(m_gasLimit), bigint(m_gasUsed)));
    }
    /// check block header of the parent
    if (_parent)
    {
        if (m_parentHash && _parent.hash() != m_parentHash)
            BOOST_THROW_EXCEPTION(InvalidParentHash());

        if (m_timestamp <= _parent.m_timestamp)
            BOOST_THROW_EXCEPTION(InvalidTimestamp());

        if (m_number != _parent.m_number + 1)
            BOOST_THROW_EXCEPTION(InvalidNumber());
    }
    /// check the stateroot and transactions of the block
    if (_block)
    {
        RLP root(_block);
        auto txList = root[1];
        auto expectedRoot = trieRootOver(txList.itemCount(), [&](unsigned i) { return rlp(i); },
            [&](unsigned i) { return txList[i].data().toBytes(); });
        LOG(WARNING) << "Expected trie root: " << expectedRoot;
        if (m_transactionsRoot != expectedRoot)
            BOOST_THROW_EXCEPTION(InvalidTransactionsRoot()
                                  << Hash256RequirementError(expectedRoot, m_transactionsRoot));
    }
}
