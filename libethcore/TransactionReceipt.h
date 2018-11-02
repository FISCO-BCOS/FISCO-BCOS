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


#include <libdevcore/Address.h>
#include <libdevcore/RLP.h>
#include <libethcore/Common.h>
#include <libethcore/LogEntry.h>
#include <libevm/ExtVMFace.h>
#include <array>

namespace dev
{
namespace eth
{
enum transaction_status_code
{
    /** Execution finished with success. */
    TRANS_SUCCESS = 0,

    /** Generic execution failure. */
    TRANS_FAILURE = 1,

    /**
     * Execution terminated with REVERT opcode.
     *
     * In this case the amount of gas left MAY be non-zero and additional output
     * data MAY be provided in ::evmc_result.
     */
    TRANS_REVERT = 2,

    /** The execution has run out of gas. */
    TRANS_OUT_OF_GAS = 3,

    /**
     * The designated INVALID instruction has been hit during execution.
     *
     * The EIP-141 (https://github.com/ethereum/EIPs/blob/master/EIPS/eip-141.md)
     * defines the instruction 0xfe as INVALID instruction to indicate execution
     * abortion coming from high-level languages. This status code is reported
     * in case this INVALID instruction has been encountered.
     */
    TRANS_INVALID_INSTRUCTION = 4,

    /** An undefined instruction has been encountered. */
    TRANS_UNDEFINED_INSTRUCTION = 5,

    /**
     * The execution has attempted to put more items on the EVM stack
     * than the specified limit.
     */
    TRANS_STACK_OVERFLOW = 6,

    /** Execution of an opcode has required more items on the EVM stack. */
    TRANS_STACK_UNDERFLOW = 7,

    /** Execution has violated the jump destination restrictions. */
    TRANS_BAD_JUMP_DESTINATION = 8,

    /**
     * Tried to read outside memory bounds.
     *
     * An example is RETURNDATACOPY reading past the available buffer.
     */
    TRANS_INVALID_MEMORY_ACCESS = 9,

    /** Call depth has exceeded the limit (if any) */
    TRANS_CALL_DEPTH_EXCEEDED = 10,

    /** Tried to execute an operation which is restricted in static mode. */
    TRANS_STATIC_MODE_VIOLATION = 11,

    /**
     * A call to a precompiled or system contract has ended with a failure.
     *
     * An example: elliptic curve functions handed invalid EC points.
     */
    TRANS_PRECOMPILE_FAILURE = 12,

    /**
     * Contract validation has failed (e.g. due to EVM 1.5 jump validity,
     * Casper's purity checker or ewasm contract rules).
     */
    TRANS_CONTRACT_VALIDATION_FAILURE = 13,

    /**
     * An argument to a state accessing method has a value outside of the
     * accepted range of values.
     */
    TRANS_ARGUMENT_OUT_OF_RANGE = 14,

    /**
     * A WebAssembly `unreachable` instruction has been hit during exection.
     */
    TRANS_WASM_UNREACHABLE_INSTRUCTION = 15,

    /**
     * A WebAssembly trap has been hit during execution. This can be for many
     * reasons, including division by zero, validation errors, etc.
     */
    TRANS_WASM_TRAP = 16,

    /** EVM implementation generic internal error. */
    TRANS_INTERNAL_ERROR = -1,

    /**
     * The execution of the given code and/or message has been rejected
     * by the EVM implementation.
     *
     * This error SHOULD be used to signal that the EVM is not able to or
     * willing to execute the given code type or message.
     * If an EVM returns the ::EVMC_REJECTED status code,
     * the Client MAY try to execute it in other EVM implementation.
     * For example, the Client tries running a code in the EVM 1.5. If the
     * code is not supported there, the execution falls back to the EVM 1.0.
     */
    TRANS_REJECTED = -2
};

class TransactionReceipt
{
public:
    TransactionReceipt(){};
    TransactionReceipt(bytesConstRef _rlp);
    TransactionReceipt(h256 _root, u256 _gasUsed, LogEntries const& _log, unsigned _status,
        bytes _bytes, Address const& _contractAddress = Address());
    TransactionReceipt(TransactionReceipt const& _other);
    h256 const& stateRoot() const { return m_stateRoot; }
    u256 const& gasUsed() const { return m_gasUsed; }
    Address const& contractAddress() const { return m_contractAddress; }
    LogBloom const& bloom() const { return m_bloom; }
    LogEntries const& log() const { return m_log; }
    unsigned const& status() const { return m_status; }
    bytes const& outputBytes() const { return m_outputBytes; }

    void streamRLP(RLPStream& _s) const;

    bytes rlp() const
    {
        RLPStream s;
        streamRLP(s);
        return s.out();
    }

private:
    h256 m_stateRoot;
    u256 m_gasUsed;
    Address m_contractAddress;
    LogBloom m_bloom;
    unsigned m_status;
    bytes m_outputBytes;
    LogEntries m_log;
};

using TransactionReceipts = std::vector<TransactionReceipt>;

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
