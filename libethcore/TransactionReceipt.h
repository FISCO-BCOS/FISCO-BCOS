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
/** @file TransactionReceipt.h
 * @author Gav Wood <i@gavwood.com>
 * @date 2014
 */

#pragma once

#include "libdevcrypto/CryptoInterface.h"
#include "libethcore/TransactionException.h"
#include <libdevcore/Address.h>
#include <libdevcore/CommonIO.h>
#include <libdevcore/RLP.h>
#include <libethcore/Common.h>
#include <libethcore/LogEntry.h>
#include <array>
namespace dev
{
namespace eth
{
class TransactionReceipt
{
public:
    typedef std::shared_ptr<TransactionReceipt> Ptr;

    TransactionReceipt() : m_status(eth::TransactionException::None){};
    TransactionReceipt(bytesConstRef _rlp);
    TransactionReceipt(h256 const& _root, u256 const& _gasUsed, LogEntries const& _log,
        eth::TransactionException _status, bytes _bytes,
        Address const& _contractAddress = Address());
    TransactionReceipt(TransactionReceipt const& _other);
    h256 const& stateRoot() const { return m_stateRoot; }
    void setStateRoot(h256 const& _stateRoot) { m_stateRoot = _stateRoot; }
    u256 const& gasUsed() const { return m_gasUsed; }
    void setGasUsed(u256 const& _gas) { m_gasUsed = _gas; }
    Address const& contractAddress() const { return m_contractAddress; }
    LogBloom const& bloom() const { return m_bloom; }
    LogEntries const& log() const { return m_log; }
    eth::TransactionException const& status() const { return m_status; }
    bytes const& outputBytes() const { return m_outputBytes; }

    void streamRLP(RLPStream& _s) const;

    void encode(bytes& receipt) const
    {
        RLPStream s;
        streamRLP(s);
        s.swapOut(receipt);
    }

    const bytes& receipt()
    {
        if (m_receipt == bytes())
        {
            encode(m_receipt);
        }
        return m_receipt;
    }

    const bytes& hash()
    {
        if (m_hash == bytes())
        {
            m_hash = crypto::Hash(receipt()).asBytes();
        }
        return m_hash;
    }

    bytes rlp() const
    {
        RLPStream s;
        streamRLP(s);
        return s.out();
    }

    void decode(bytesConstRef receiptsBytes);
    void decode(RLP const& rlp);

private:
    h256 m_stateRoot;
    u256 m_gasUsed;
    Address m_contractAddress;
    LogBloom m_bloom;

protected:
    eth::TransactionException m_status;

private:
    bytes m_outputBytes;
    LogEntries m_log;

private:
    bytes m_receipt = bytes();
    bytes m_hash = bytes();
};

using TransactionReceipts = std::vector<TransactionReceipt::Ptr>;

std::ostream& operator<<(std::ostream& _out, eth::TransactionReceipt const& _r);

class LocalisedTransactionReceipt : public TransactionReceipt
{
public:
    using Ptr = std::shared_ptr<LocalisedTransactionReceipt>;
    LocalisedTransactionReceipt(TransactionReceipt const& _t, h256 const& _hash,
        h256 const& _blockHash, BlockNumber _blockNumber, Address const& _from, Address const& _to,
        unsigned _transactionIndex, u256 const& _gasUsed,
        Address const& _contractAddress = Address())
      : TransactionReceipt(_t),
        m_hash(_hash),
        m_blockHash(_blockHash),
        m_blockNumber(_blockNumber),
        m_from(_from),
        m_to(_to),
        m_transactionIndex(_transactionIndex),
        m_gasUsed(_gasUsed),
        m_contractAddress(_contractAddress)
    {
        LogEntries entries = log();
        for (unsigned i = 0; i < entries.size(); i++)
            m_localisedLogs.push_back(LocalisedLogEntry(
                entries[i], m_blockHash, m_blockNumber, m_hash, m_transactionIndex, i));
    }

    LocalisedTransactionReceipt(eth::TransactionException status) : m_blockNumber(0)
    {
        TransactionReceipt::m_status = status;
    };

    h256 const& hash() const { return m_hash; }
    h256 const& blockHash() const { return m_blockHash; }
    BlockNumber blockNumber() const { return m_blockNumber; }
    Address const& from() const { return m_from; }
    Address const& to() const { return m_to; }
    unsigned transactionIndex() const { return m_transactionIndex; }
    u256 const& gasUsed() const { return m_gasUsed; }
    Address const& contractAddress() const { return m_contractAddress; }
    LocalisedLogEntries const& localisedLogs() const { return m_localisedLogs; };

private:
    h256 m_hash;
    h256 m_blockHash;
    BlockNumber m_blockNumber;
    Address m_from;
    Address m_to;
    unsigned m_transactionIndex = 0;
    u256 m_gasUsed;
    Address m_contractAddress;
    LocalisedLogEntries m_localisedLogs;
};

}  // namespace eth
}  // namespace dev
