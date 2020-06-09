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

#pragma once

#include "Common.h"
#include <libethcore/BlockHeader.h>
#include <libethcore/Common.h>
#include <libethcore/EVMFlags.h>
#include <libethcore/Transaction.h>
#include <functional>

namespace Json
{
class Value;
}

namespace dev
{
class OverlayDB;

namespace storage
{
class TableFactory;
}

namespace eth
{
class Block;
class Result;
}  // namespace eth
namespace precompiled
{
class PrecompiledExecResult;
}

/**
 * @brief Message-call/contract-creation executor; useful for executing transactions.
 *
 * Two ways of using this class - either as a transaction executive or a CALL/CREATE executive.
 *
 * In the first use, after construction, begin with initialize(), then execute() and end with
 * finalize(). Call go() after execute() only if it returns false.
 *
 * In the second use, after construction, begin with call() or create() and end with
 * accrueSubState(). Call go() after call()/create() only if it returns false.
 *
 * Example:
 * @code
 * Executive e(state, blockchain, 0);
 * e.initialize(transaction);
 * if (!e.execute())
 *    e.go();
 * e.finalize();
 * @endcode
 */
namespace executive
{
class EVMHostContext;
class StateFace;
class Executive
{
public:
    using Ptr = std::shared_ptr<Executive>;
    /// Simple constructor; executive will operate on given state, with the given environment info.
    Executive(
        std::shared_ptr<StateFace> _s, dev::executive::EnvInfo const& _envInfo, unsigned _level = 0)
      : m_s(_s), m_envInfo(_envInfo), m_depth(_level)
    {}

    Executive() {}


    // template <typename T>
    // Executive(T _s, dev::executive::EnvInfo const& _envInfo, unsigned _level = 0)
    //  : m_s(dynamic_cast<std::shared_ptr<StateFace>>(_s)), m_envInfo(_envInfo), m_depth(_level)
    //{}

    /** Easiest constructor.
     * Creates executive to operate on the state of end of the given block, populating environment
     * info from given Block and the LastHashes portion from the BlockChain.
     */
    // Executive(Block& _s, BlockChain const& _bc, unsigned _level = 0);

    /** Previous-state constructor.
     * Creates executive to operate on the state of a particular transaction in the given block,
     * populating environment info from the given Block and the LastHashes portion from the
     * BlockChain. State is assigned the resultant value, but otherwise unused.
     */
    // Executive(StateFace& io_s, Block const& _block, unsigned _txIndex, BlockChain const& _bc,
    // unsigned _level = 0);

    Executive(Executive const&) = delete;
    void operator=(Executive) = delete;

    /// Initializes the executive for evaluating a transaction. You must call finalize() at some
    /// point following this.
    void initialize(bytesConstRef _transaction)
    {
        initialize(std::make_shared<dev::eth::Transaction>(
            _transaction, dev::eth::CheckTransaction::None));
    }
    void initialize(dev::eth::Transaction::Ptr _transaction);
    /// Finalise a transaction previously set up with initialize().
    /// @warning Only valid after initialize() and execute(), and possibly go().
    /// @returns true if the outermost execution halted normally, false if exceptionally halted.
    bool finalize();
    /// Begins execution of a transaction. You must call finalize() following this.
    /// @returns true if the transaction is done, false if go() must be called.

    void verifyTransaction(dev::eth::ImportRequirements::value _ir, dev::eth::Transaction::Ptr _t,
        dev::eth::BlockHeader const& _header, u256 const& _gasUsed);

    bool execute();
    /// @returns the transaction from initialize().
    /// @warning Only valid after initialize().
    dev::eth::Transaction::Ptr tx() const { return m_t; }
    /// @returns the log entries created by this operation.
    /// @warning Only valid after finalise().
    dev::eth::LogEntries const& logs() const { return m_logs; }
    /// @returns total gas used in the transaction/operation.
    /// @warning Only valid after finalise().
    u256 gasUsed() const;

    owning_bytes_ref takeOutput() { return std::move(m_output); }

    /// Set up the executive for evaluating a bare CREATE (contract-creation) operation.
    /// @returns false iff go() must be called (and thus a VM execution in required).
    bool create(Address const& _txSender, u256 const& _endowment, u256 const& _gasPrice,
        u256 const& _gas, bytesConstRef _code, Address const& _originAddress);
    /// @returns false iff go() must be called (and thus a VM execution in required).
    bool createOpcode(Address const& _sender, u256 const& _endowment, u256 const& _gasPrice,
        u256 const& _gas, bytesConstRef _code, Address const& _originAddress);
    /// @returns false iff go() must be called (and thus a VM execution in required).
    bool create2Opcode(Address const& _sender, u256 const& _endowment, u256 const& _gasPrice,
        u256 const& _gas, bytesConstRef _code, Address const& _originAddress, u256 const& _salt);
    /// Set up the executive for evaluating a bare CALL (message call) operation.
    /// @returns false iff go() must be called (and thus a VM execution in required).
    bool call(Address const& _receiveAddress, Address const& _txSender, u256 const& _txValue,
        u256 const& _gasPrice, bytesConstRef _txData, u256 const& _gas);
    bool call(
        dev::executive::CallParameters const& _cp, u256 const& _gasPrice, Address const& _origin);
    bool callRC2(
        dev::executive::CallParameters const& _cp, u256 const& _gasPrice, Address const& _origin);
    /// Finalise an operation through accruing the substate into the parent context.
    void accrueSubState(dev::executive::SubState& _parentContext);

    /// Executes (or continues execution of) the VM.
    /// @returns false iff go() must be called again to finish the transaction.
    bool go();

    /// @returns gas remaining after the transaction/operation. Valid after the transaction has been
    /// executed.
    u256 gas() const { return m_gas; }
    eth::TransactionException status() const { return m_excepted; }
    /// @returns the new address for the created contract in the CREATE operation.
    Address newAddress() const { return m_newAddress; }

    /// @returns The exception that has happened during the execution if any.
    eth::TransactionException getException() const noexcept { return m_excepted; }

    /// Revert all changes made to the state by this execution.
    void revert();

    /// print exception to log
    void loggingException();

    void reset()
    {
        m_ext = nullptr;
        m_output = owning_bytes_ref();
        m_depth = 0;
        m_excepted = eth::TransactionException::None;
        m_exceptionReason.clear();
        m_baseGasRequired = 0;
        m_gas = 0;
        m_refunded = 0;
        m_gasCost = 0;
        m_isCreation = false;
        m_newAddress = Address();
        m_savepoint = 0;
        m_tableFactorySavepoint = 0;
        m_logs.clear();
        m_t.reset();
    }

    void setEnvInfo(dev::executive::EnvInfo const& _envInfo) { m_envInfo = _envInfo; }

    void setState(std::shared_ptr<StateFace> _state) { m_s = _state; }

    void setEvmFlags(VMFlagType const& _evmFlags)
    {
        m_evmFlags = _evmFlags;
        m_enableFreeStorage = enableFreeStorage(_evmFlags);
    }

private:
    void parseEVMCResult(std::shared_ptr<eth::Result> _result);
    /// @returns false iff go() must be called (and thus a VM execution in required).
    bool executeCreate(Address const& _txSender, u256 const& _endowment, u256 const& _gasPrice,
        u256 const& _gas, bytesConstRef _code, Address const& _originAddress);

    void grantContractStatusManager(std::shared_ptr<dev::storage::TableFactory> memoryTableFactory,
        Address const& newAddress, Address const& sender, Address const& origin);

    void writeErrInfoToOutput(std::string const& errInfo);

    void updateGas(std::shared_ptr<dev::precompiled::PrecompiledExecResult> _callResult);

    std::shared_ptr<StateFace> m_s;  ///< The state to which this operation/transaction is applied.
    // TODO: consider changign to EnvInfo const& to avoid LastHashes copy at every CALL/CREATE
    dev::executive::EnvInfo m_envInfo;      ///< Information on the runtime environment.
    std::shared_ptr<EVMHostContext> m_ext;  ///< The VM externality object for the VM execution or
                                            ///< null if no VM is required. shared_ptr used only to
                                            ///< allow EVMHostContext forward reference. This field
                                            ///< does *NOT* survive this object.
    owning_bytes_ref m_output;              ///< Execution output.

    unsigned m_depth = 0;  ///< The context's call-depth.
    eth::TransactionException m_excepted =
        eth::TransactionException::None;  ///< Details if the VM's execution resulted in an
                                          ///< exception.
    std::stringstream m_exceptionReason;

    int64_t m_baseGasRequired;  ///< The base amount of gas requried for executing this transaction.
    u256 m_gas = 0;       ///< The gas for EVM code execution. Initial amount before go() execution,
                          ///< final amount after go() execution.
    u256 m_refunded = 0;  ///< The amount of gas refunded.

    dev::eth::Transaction::Ptr m_t;  ///< The original transaction. Set by setup().
    dev::eth::LogEntries m_logs;     ///< The log entries created by this transaction. Set by
                                     ///< finalize().

    u256 m_gasCost;

    bool m_isCreation = false;
    Address m_newAddress;
    size_t m_savepoint = 0;
    size_t m_tableFactorySavepoint = 0;

    VMFlagType m_evmFlags;
    // determine whether the freeStorageVMSchedule enabled or not
    bool m_enableFreeStorage = false;
};

}  // namespace executive
}  // namespace dev
