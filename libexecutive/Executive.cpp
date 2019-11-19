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

#include "Executive.h"
#include "ExtVM.h"
#include "StateFace.h"

#include <libdevcore/CommonIO.h>
#include <libethcore/CommonJS.h>
#include <libethcore/EVMSchedule.h>
#include <libethcore/LastBlockHashesFace.h>
#include <libevm/VMFactory.h>
#include <libstorage/Common.h>
#include <libstorage/MemoryTableFactory.h>
#include <libstorage/StorageException.h>

#include <json/json.h>
#include <libblockverifier/ExecutiveContext.h>
#include <boost/timer.hpp>
#include <numeric>

using namespace std;
using namespace dev;
using namespace dev::eth;
using namespace dev::executive;
using namespace dev::storage;

u256 Executive::gasUsed() const
{
    return m_envInfo.precompiledEngine()->txGasLimit() - m_gas;
}

void Executive::accrueSubState(SubState& _parentContext)
{
    if (m_ext)
        _parentContext += m_ext->sub();
}

void Executive::initialize(Transaction::Ptr _transaction)
{
    m_t = _transaction;
    m_baseGasRequired = m_t->baseGasRequired(g_BCOSConfig.evmSchedule());

    verifyTransaction(ImportRequirements::Everything, m_t, m_envInfo.header(), m_envInfo.gasUsed());

    if (!m_t->hasZeroSignature())
    {
        // No need nonce increasing sequently at all. See random id for more.

        // Avoid unaffordable transactions.
        bigint gasCost = (bigint)m_t->gas() * m_t->gasPrice();
        bigint totalCost = m_t->value() + gasCost;
        m_gasCost = (u256)gasCost;  // Convert back to 256-bit, safe now.
    }
}

void Executive::verifyTransaction(
    ImportRequirements::value _ir, Transaction::Ptr _t, BlockHeader const&, u256 const&)
{
    eth::EVMSchedule const& schedule = g_BCOSConfig.evmSchedule();

    uint64_t txGasLimit = m_envInfo.precompiledEngine()->txGasLimit();
    // The gas limit is dynamic, not fixed.
    // Pre calculate the gas needed for execution
    if ((_ir & ImportRequirements::TransactionBasic) &&
        _t->baseGasRequired(schedule) > (bigint)txGasLimit)
    {
        m_excepted = TransactionException::OutOfGasIntrinsic;
        m_exceptionReason
            << LOG_KV("reason",
                   "The gas required by deploying this contract is more than tx_gas_limit")
            << LOG_KV("limit", txGasLimit) << LOG_KV("require", _t->baseGasRequired(schedule));
        BOOST_THROW_EXCEPTION(OutOfGasIntrinsic() << RequirementError(
                                  (bigint)(_t->baseGasRequired(schedule)), (bigint)txGasLimit));
    }
}

bool Executive::execute()
{
    uint64_t txGasLimit = m_envInfo.precompiledEngine()->txGasLimit();

    if (g_BCOSConfig.version() > RC3_VERSION)
    {
        if (txGasLimit < (u256)m_baseGasRequired)
        {
            m_excepted = TransactionException::OutOfGasBase;
            m_exceptionReason
                << LOG_KV("reason",
                       "The gas required by deploying this contract is more than tx_gas_limit")
                << LOG_KV("limit", txGasLimit) << LOG_KV("require", m_baseGasRequired);
            BOOST_THROW_EXCEPTION(
                OutOfGasBase() << errinfo_comment(
                    "Not enough gas, base gas required:" + std::to_string(m_baseGasRequired)));
        }
    }
    else
    {
        if (m_t->gas() < (u256)m_baseGasRequired)
        {
            m_excepted = TransactionException::OutOfGasBase;
            m_exceptionReason
                << LOG_KV("reason",
                       "The gas required by deploying this contract is more than sender given")
                << LOG_KV("given", m_t->gas()) << LOG_KV("require", m_baseGasRequired);
            BOOST_THROW_EXCEPTION(
                OutOfGasBase() << errinfo_comment(
                    "Not enough gas, base gas required:" + std::to_string(m_baseGasRequired)));
        }
    }
    if (m_t->isCreation())
        return create(m_t->sender(), m_t->value(), m_t->gasPrice(),
            txGasLimit - (u256)m_baseGasRequired, &m_t->data(), m_t->sender());
    else
        return call(m_t->receiveAddress(), m_t->sender(), m_t->value(), m_t->gasPrice(),
            bytesConstRef(&m_t->data()), txGasLimit - (u256)m_baseGasRequired);
}

bool Executive::call(Address const& _receiveAddress, Address const& _senderAddress,
    u256 const& _value, u256 const& _gasPrice, bytesConstRef _data, u256 const& _gas)
{
    CallParameters params{
        _senderAddress, _receiveAddress, _receiveAddress, _value, _value, _gas, _data, {}};
    return call(params, _gasPrice, _senderAddress);
}


bool Executive::call(CallParameters const& _p, u256 const& _gasPrice, Address const& _origin)
{
    if (g_BCOSConfig.version() >= RC2_VERSION)
    {
        return callRC2(_p, _gasPrice, _origin);
    }
    // If external transaction.
    if (m_t)
    {
        // FIXME: changelog contains unrevertable balance change that paid
        //        for the transaction.
        // Increment associated nonce for sender.
        // if (_p.senderAddress != MaxAddress ||
        // m_envInfo.number() < m_sealEngine.chainParams().experimentalForkBlock)  // EIP86
        m_s->incNonce(_p.senderAddress);
    }

    m_savepoint = m_s->savepoint();
    m_tableFactorySavepoint = m_envInfo.precompiledEngine()->getMemoryTableFactory()->savepoint();
    try
    {
        if (m_envInfo.precompiledEngine() &&
            m_envInfo.precompiledEngine()->isOrginPrecompiled(_p.codeAddress))
        {
            m_gas = _p.gas;
            bytes output;
            bool success;
            tie(success, output) =
                m_envInfo.precompiledEngine()->executeOriginPrecompiled(_p.codeAddress, _p.data);
            size_t outputSize = output.size();
            m_output = owning_bytes_ref{std::move(output), 0, outputSize};
        }
        else if (m_envInfo.precompiledEngine() &&
                 m_envInfo.precompiledEngine()->isPrecompiled(_p.codeAddress))
        {
            m_gas = _p.gas;
            LOG(TRACE) << "Execute Precompiled: " << _p.codeAddress;
            auto result = m_envInfo.precompiledEngine()->call(_origin, _p.codeAddress, _p.data);
            size_t outputSize = result.size();
            m_output = owning_bytes_ref{std::move(result), 0, outputSize};
        }
        else
        {
            m_gas = _p.gas;
            if (m_s->addressHasCode(_p.codeAddress))
            {
                bytes const& c = m_s->code(_p.codeAddress);
                h256 codeHash = m_s->codeHash(_p.codeAddress);
                m_ext = make_shared<ExtVM>(m_s, m_envInfo, _p.receiveAddress, _p.senderAddress,
                    _origin, _p.apparentValue, _gasPrice, _p.data, &c, codeHash, m_depth, false,
                    _p.staticCall);
            }
        }
        // Transfer ether.
        m_s->transferBalance(_p.senderAddress, _p.receiveAddress, _p.valueTransfer);
    }
    catch (dev::eth::PrecompiledError const& e)
    {
        revert();
        m_excepted = TransactionException::PrecompiledError;
    }
    catch (NotEnoughCash const& _e)
    {
        revert();
        m_excepted = TransactionException::NotEnoughCash;
    }
    catch (std::exception const& e)
    {
        revert();
        m_excepted = TransactionException::Unknown;
    }
    return !m_ext;
}

bool Executive::callRC2(CallParameters const& _p, u256 const& _gasPrice, Address const& _origin)
{
    // no nonce increase

    m_savepoint = m_s->savepoint();
    m_tableFactorySavepoint = m_envInfo.precompiledEngine()->getMemoryTableFactory()->savepoint();
    m_gas = _p.gas;
    if (m_envInfo.precompiledEngine() &&
        m_envInfo.precompiledEngine()->isOrginPrecompiled(_p.codeAddress))
    {
        bytes output;
        bool success;
        tie(success, output) =
            m_envInfo.precompiledEngine()->executeOriginPrecompiled(_p.codeAddress, _p.data);
        size_t outputSize = output.size();
        m_output = owning_bytes_ref{std::move(output), 0, outputSize};
    }
    else if (m_envInfo.precompiledEngine() &&
             m_envInfo.precompiledEngine()->isPrecompiled(_p.codeAddress))
    {
        try
        {
            auto result = m_envInfo.precompiledEngine()->call(_origin, _p.codeAddress, _p.data);
            size_t outputSize = result.size();
            m_output = owning_bytes_ref{std::move(result), 0, outputSize};
        }
        catch (dev::Exception& e)
        {
            revert();
            m_excepted = toTransactionException(e);
        }
        catch (std::exception& e)
        {
            revert();
            m_excepted = TransactionException::Unknown;
        }
    }
    else if (m_s->addressHasCode(_p.codeAddress))
    {
        bytes const& c = m_s->code(_p.codeAddress);
        h256 codeHash = m_s->codeHash(_p.codeAddress);
        m_ext = make_shared<ExtVM>(m_s, m_envInfo, _p.receiveAddress, _p.senderAddress, _origin,
            _p.apparentValue, _gasPrice, _p.data, &c, codeHash, m_depth, false, _p.staticCall);
    }
    else
    {
        m_excepted = TransactionException::CallAddressError;
    }

    // no balance transfer
    return !m_ext;
}

bool Executive::create(Address const& _txSender, u256 const& _endowment, u256 const& _gasPrice,
    u256 const& _gas, bytesConstRef _init, Address const& _origin)
{
    // Contract creation by an external account is the same as CREATE opcode
    return createOpcode(_txSender, _endowment, _gasPrice, _gas, _init, _origin);
}

bool Executive::createOpcode(Address const& _sender, u256 const& _endowment, u256 const& _gasPrice,
    u256 const& _gas, bytesConstRef _init, Address const& _origin)
{
    u256 nonce = m_s->getNonce(_sender);
    m_newAddress = right160(sha3(rlpList(_sender, nonce)));
    return executeCreate(_sender, _endowment, _gasPrice, _gas, _init, _origin);
}

bool Executive::create2Opcode(Address const& _sender, u256 const& _endowment, u256 const& _gasPrice,
    u256 const& _gas, bytesConstRef _init, Address const& _origin, u256 const& _salt)
{
    m_newAddress =
        right160(sha3(bytes{0xff} + _sender.asBytes() + toBigEndian(_salt) + sha3(_init)));
    return executeCreate(_sender, _endowment, _gasPrice, _gas, _init, _origin);
}

bool Executive::executeCreate(Address const& _sender, u256 const& _endowment, u256 const& _gasPrice,
    u256 const& _gas, bytesConstRef _init, Address const& _origin)
{
    // check authority for deploy contract
    auto memeryTableFactory = m_envInfo.precompiledEngine()->getMemoryTableFactory();
    auto table = memeryTableFactory->openTable(SYS_TABLES);
    if (!table->checkAuthority(_origin))
    {
        LOG(WARNING) << "Executive deploy contract checkAuthority of " << _origin.hex()
                     << " failed!";
        m_gas = 0;
        m_excepted = TransactionException::PermissionDenied;
        revert();
        m_ext = {};
        return !m_ext;
    }

    m_s->incNonce(_sender);

    m_savepoint = m_s->savepoint();
    m_tableFactorySavepoint = m_envInfo.precompiledEngine()->getMemoryTableFactory()->savepoint();

    m_isCreation = true;

    // We can allow for the reverted state (i.e. that with which m_ext is constructed) to contain
    // the m_orig.address, since we delete it explicitly if we decide we need to revert.

    m_gas = _gas;
    bool accountAlreadyExist =
        (m_s->addressHasCode(m_newAddress) || m_s->getNonce(m_newAddress) > 0);
    if (accountAlreadyExist)
    {
        LOG(TRACE) << "Executive Address already used: " << m_newAddress;
        m_gas = 0;
        m_excepted = TransactionException::AddressAlreadyUsed;
        revert();
        m_ext = {};  // cancel the _init execution if there are any scheduled.
        return !m_ext;
    }

    // Transfer ether before deploying the code. This will also create new
    // account if it does not exist yet.
    m_s->transferBalance(_sender, m_newAddress, _endowment);

    u256 newNonce = m_s->requireAccountStartNonce();
    // if (m_envInfo.number() >= m_sealEngine.chainParams().EIP158ForkBlock)
    // newNonce += 1;
    m_s->setNonce(m_newAddress, newNonce);

    // Schedule _init execution if not empty.
    if (!_init.empty())
        m_ext = make_shared<ExtVM>(m_s, m_envInfo, m_newAddress, _sender, _origin, _endowment,
            _gasPrice, bytesConstRef(), _init, sha3(_init), m_depth, true, false);

    return !m_ext;
}

bool Executive::go(OnOpFunc const& _onOp)
{
    if (m_ext)
    {
#if ETH_TIMED_EXECUTIONS
        Timer t;
#endif
        try
        {
            // Create VM instance. Force Interpreter if tracing requested.
            auto vm = VMFactory::create();
            if (m_isCreation)
            {
                m_s->clearStorage(m_ext->myAddress());
                auto out = vm->exec(m_gas, *m_ext, _onOp);

                m_res.gasForDeposit = m_gas;
                m_res.depositSize = out.size();

                if (out.size() > m_ext->evmSchedule().maxCodeSize)
                {
                    m_exceptionReason << LOG_KV("reason", "Code is too long")
                                      << LOG_KV("size_limit", m_ext->evmSchedule().maxCodeSize)
                                      << LOG_KV("size", out.size());
                    BOOST_THROW_EXCEPTION(OutOfGas());
                }
                else if (out.size() * m_ext->evmSchedule().createDataGas <= m_gas)
                {
                    m_res.codeDeposit = CodeDeposit::Success;
                    m_gas -= out.size() * m_ext->evmSchedule().createDataGas;
                }
                else
                {
                    if (m_ext->evmSchedule().exceptionalFailedCodeDeposit)
                    {
                        m_exceptionReason << LOG_KV("reason", "exceptionalFailedCodeDeposit");
                        BOOST_THROW_EXCEPTION(OutOfGas());
                    }
                    else
                    {
                        m_res.codeDeposit = CodeDeposit::Failed;
                        out = {};
                    }
                }

                m_res.output = out.toVector();  // copy output to execution result
                m_s->setCode(m_ext->myAddress(), out.toVector());
            }
            else
                m_output = vm->exec(m_gas, *m_ext, _onOp);
        }
        catch (RevertInstruction& _e)
        {
            revert();
            m_output = _e.output();
            m_excepted = TransactionException::RevertInstruction;
        }
        catch (OutOfGas& _e)
        {
            revert();
            m_excepted = TransactionException::OutOfGas;
        }
        catch (GasOverflow const& _e)
        {
            revert();
            m_excepted = TransactionException::GasOverflow;
        }
        catch (VMException const& _e)
        {
            LOG(TRACE) << "Safe VM Exception. " << diagnostic_information(_e);
            m_gas = 0;
            m_excepted = toTransactionException(_e);
            revert();
        }
        catch (PermissionDenied const& _e)
        {
            revert();
            m_excepted = TransactionException::PermissionDenied;
        }
        catch (NotEnoughCash const& _e)
        {
            revert();
            m_excepted = TransactionException::NotEnoughCash;
        }
        catch (PrecompiledError const& _e)
        {
            revert();
            m_excepted = TransactionException::PrecompiledError;
        }
        catch (InternalVMError const& _e)
        {
            using errinfo_evmcStatusCode =
                boost::error_info<struct tag_evmcStatusCode, evmc_status_code>;
            LOG(WARNING) << "Internal VM Error ("
                         << *boost::get_error_info<errinfo_evmcStatusCode>(_e) << ")\n"
                         << diagnostic_information(_e);
            revert();
            exit(1);
        }
        catch (Exception const& _e)
        {
            // TODO: AUDIT: check that this can never reasonably happen. Consider what to do if it
            // does.
            LOG(ERROR) << "Unexpected exception in VM. There may be a bug in this implementation. "
                       << diagnostic_information(_e);
            exit(1);
            // Another solution would be to reject this transaction, but that also
            // has drawbacks. Essentially, the amount of ram has to be increased here.
        }
        catch (std::exception const& _e)
        {
            // TODO: AUDIT: check that this can never reasonably happen. Consider what to do if it
            // does.
            LOG(ERROR) << "Unexpected std::exception in VM. Not enough RAM? " << _e.what();
            exit(1);
            // Another solution would be to reject this transaction, but that also
            // has drawbacks. Essentially, the amount of ram has to be increased here.
        }

        if (m_output)
            // Copy full output:
            m_res.output = m_output.toVector();

#if ETH_TIMED_EXECUTIONS
        cnote << "VM took:" << t.elapsed() << "; gas used: " << (sgas - m_endGas);
#endif
    }
    return true;
}

bool Executive::finalize()
{
    // Accumulate refunds for suicides.
    if (m_ext)
        m_ext->sub().refunds +=
            m_ext->evmSchedule().suicideRefundGas * m_ext->sub().suicides.size();

    // SSTORE refunds...
    // must be done before the sealer gets the fees.
    m_refunded = m_ext ? min((m_t->gas() - m_gas) / 2, m_ext->sub().refunds) : 0;
    m_gas += m_refunded;

    // Suicides...
    if (m_ext)
        for (auto a : m_ext->sub().suicides)
            m_s->kill(a);

    // Logs..
    if (m_ext)
        m_logs = m_ext->sub().logs;


    m_res.gasUsed = gasUsed();
    m_res.excepted = m_excepted;  // TODO: m_except is used only in ExtVM::call
    m_res.newAddress = m_newAddress;
    m_res.gasRefunded = m_ext ? m_ext->sub().refunds : 0;

    return (m_excepted == TransactionException::None);
}

void Executive::revert()
{
    if (m_ext)
        m_ext->sub().clear();

    // Set result address to the null one.
    m_newAddress = {};
    m_s->rollback(m_savepoint);
    auto memoryTableFactory = m_envInfo.precompiledEngine()->getMemoryTableFactory();
    memoryTableFactory->rollback(m_tableFactorySavepoint);
}


void Executive::loggingException()
{
    if (m_excepted != TransactionException::None)
    {
        LOG(ERROR) << LOG_BADGE("TxExeError") << LOG_DESC("Transaction execution error")
                   << LOG_KV("hash", (!m_t->hasZeroSignature()) ? m_t->sha3().abridged() : "call")
                   << m_exceptionReason.str();
    }
}