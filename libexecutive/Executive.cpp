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
#include "EVMHostContext.h"
#include "EVMHostInterface.h"
#include "EVMInstance.h"
#include "StateFace.h"
#include <json/json.h>
#include <libblockverifier/ExecutiveContext.h>
#include <libdevcore/CommonIO.h>
#include <libethcore/ABI.h>
#include <libethcore/CommonJS.h>
#include <libethcore/EVMSchedule.h>
#include <libexecutive/EVMInterface.h>
#include <libexecutive/VMFactory.h>
#include <libstorage/Common.h>
#include <libstorage/MemoryTableFactory.h>
#include <libstorage/StorageException.h>
#include <limits.h>
#include <boost/timer.hpp>
#include <numeric>

using namespace std;
using namespace dev;
using namespace dev::eth;
using namespace dev::executive;
using namespace dev::storage;

/// Error info for EVMInstance status code.
using errinfo_evmcStatusCode = boost::error_info<struct tag_evmcStatusCode, evmc_status_code>;

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
    if (m_enableFreeStorage)
    {
        m_baseGasRequired = 20000;
    }
    else
    {
        m_baseGasRequired = m_t->baseGasRequired(g_BCOSConfig.evmSchedule());
    }

    verifyTransaction(ImportRequirements::Everything, m_t, m_envInfo.header(), m_envInfo.gasUsed());

    if (!m_t->hasZeroSignature())
    {
        // No need nonce increasing sequently at all. See random id for more.

        // Avoid unaffordable transactions.
        bigint gasCost = (bigint)m_t->gas() * m_t->gasPrice();
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
        m_exceptionReason << LOG_KV("reason",
                                 "The gas required by deploying/accessing this contract is more "
                                 "than tx_gas_limit")
                          << LOG_KV("limit", txGasLimit)
                          << LOG_KV("require", _t->baseGasRequired(schedule));
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
            m_exceptionReason << LOG_KV("reason",
                                     "The gas required by deploying/accessing this contract is "
                                     "more than tx_gas_limit")
                              << LOG_KV("limit", txGasLimit)
                              << LOG_KV("require", m_baseGasRequired);
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
    {
        return create(m_t->sender(), m_t->value(), m_t->gasPrice(),
            txGasLimit - (u256)m_baseGasRequired, &m_t->data(), m_t->sender());
    }
    else
    {
        return call(m_t->receiveAddress(), m_t->sender(), m_t->value(), m_t->gasPrice(),
            bytesConstRef(&m_t->data()), txGasLimit - (u256)m_baseGasRequired);
    }
}

bool Executive::call(Address const& _receiveAddress, Address const& _senderAddress,
    u256 const& _value, u256 const& _gasPrice, bytesConstRef _data, u256 const& _gas)
{
    CallParameters params{
        _senderAddress, _receiveAddress, _receiveAddress, _value, _value, _gas, _data};
    return call(params, _gasPrice, _senderAddress);
}

void Executive::updateGas(std::shared_ptr<dev::precompiled::PrecompiledExecResult> _callResult)
{
    // calculate gas
    auto gasUsed = _callResult->calGasCost();
    if (m_gas < gasUsed)
    {
        m_excepted = TransactionException::OutOfGas;
        LOG(WARNING) << LOG_DESC("OutOfGas when executing precompiled Contract")
                     << LOG_KV("gasUsed", gasUsed) << LOG_KV("curGas", m_gas);
        BOOST_THROW_EXCEPTION(dev::precompiled::PrecompiledException(
            "OutOfGas when executing precompiled Contract, gasUsed: " +
            boost::lexical_cast<std::string>(gasUsed) +
            ", leftGas:" + boost::lexical_cast<std::string>(m_gas)));
    }
    m_gas -= gasUsed;
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
            auto callResult = m_envInfo.precompiledEngine()->call(
                _p.codeAddress, _p.data, _origin, _p.senderAddress);
            size_t outputSize = callResult->execResult().size();
            auto output = callResult->execResult();
            m_output = owning_bytes_ref{std::move(output), 0, outputSize};
        }
        else
        {
            m_gas = _p.gas;
            if (m_s->frozen(_p.codeAddress))
            {
                LOG(DEBUG) << LOG_DESC("execute transaction failed for ContractFrozen")
                           << LOG_KV("contractAddr", _p.codeAddress);
                m_excepted = TransactionException::ContractFrozen;
            }
            else if (m_s->addressHasCode(_p.codeAddress))
            {
                bytes const& c = m_s->code(_p.codeAddress);
                h256 codeHash = m_s->codeHash(_p.codeAddress);
                m_ext = make_shared<EVMHostContext>(m_s, m_envInfo, _p.receiveAddress,
                    _p.senderAddress, _origin, _p.apparentValue, _gasPrice, _p.data, c, codeHash,
                    m_depth, false, _p.staticCall);
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

    if (g_BCOSConfig.version() >= V2_5_0 && m_t && m_s->frozen(_origin))
    {
        LOG(DEBUG) << LOG_DESC("execute transaction failed for account frozen")
                   << LOG_KV("account", _origin);
        writeErrInfoToOutput("Frozen account:0x" + _origin.hex());
        revert();
        m_excepted = TransactionException::AccountFrozen;
        return !m_ext;
    }

    if (m_envInfo.precompiledEngine() &&
        m_envInfo.precompiledEngine()->isOrginPrecompiled(_p.codeAddress))
    {
        if (g_BCOSConfig.version() >= V2_5_0)
        {
            auto gas = m_envInfo.precompiledEngine()->costOfPrecompiled(_p.codeAddress, _p.data);
            if (m_gas < gas)
            {
                m_excepted = TransactionException::OutOfGasBase;
                // true actually means "all finished - nothing more to be done regarding go().
                return true;
            }
            else
            {
                m_gas = (u256)(_p.gas - gas);
            }
        }
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
            auto callResult = m_envInfo.precompiledEngine()->call(
                _p.codeAddress, _p.data, _origin, _p.senderAddress);
            // only calculate gas for the precompiled contract after v2.4.0
            if (g_BCOSConfig.version() >= V2_4_0)
            {
                updateGas(callResult);
            }
            size_t outputSize = callResult->execResult().size();
            auto output = callResult->execResult();
            m_output = owning_bytes_ref{std::move(output), 0, outputSize};
        }
        catch (dev::precompiled::PrecompiledException& e)
        {
            revert();
            m_excepted = TransactionException::PrecompiledError;
            auto output = e.ToOutput();
            m_output = owning_bytes_ref{std::move(output), 0, output.size()};
        }
        catch (dev::Exception& e)
        {
            if (g_BCOSConfig.version() >= V2_3_0)
            {
                writeErrInfoToOutput(e.what());
            }
            revert();
            m_excepted = toTransactionException(e);
        }
        catch (std::exception& e)
        {
            if (g_BCOSConfig.version() >= V2_3_0)
            {
                writeErrInfoToOutput(e.what());
            }
            revert();
            m_excepted = TransactionException::Unknown;
        }
    }
    else if (m_s->frozen(_p.codeAddress))
    {
        LOG(DEBUG) << LOG_DESC("execute transaction failed for ContractFrozen")
                   << LOG_KV("contractAddr", _p.codeAddress);
        writeErrInfoToOutput("Frozen contract:" + _p.codeAddress.hex());
        revert();
        m_excepted = TransactionException::ContractFrozen;
    }
    else if (m_s->addressHasCode(_p.codeAddress))
    {
        bytes const& c = m_s->code(_p.codeAddress);
        h256 codeHash = m_s->codeHash(_p.codeAddress);
        m_ext = make_shared<EVMHostContext>(m_s, m_envInfo, _p.receiveAddress, _p.senderAddress,
            _origin, _p.apparentValue, _gasPrice, _p.data, c, codeHash, m_depth, false,
            _p.staticCall);
        m_ext->setEvmFlags(m_evmFlags);
    }
    else
    {
        if (g_BCOSConfig.version() >= V2_3_0)
        {
            writeErrInfoToOutput("Error address:" + _p.codeAddress.hex());
            revert();
        }
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
    m_newAddress = right160(crypto::Hash(rlpList(_sender, nonce)));
    return executeCreate(_sender, _endowment, _gasPrice, _gas, _init, _origin);
}

bool Executive::create2Opcode(Address const& _sender, u256 const& _endowment, u256 const& _gasPrice,
    u256 const& _gas, bytesConstRef _init, Address const& _origin, u256 const& _salt)
{
    m_newAddress = right160(
        crypto::Hash(bytes{0xff} + _sender.asBytes() + toBigEndian(_salt) + crypto::Hash(_init)));
    return executeCreate(_sender, _endowment, _gasPrice, _gas, _init, _origin);
}

bool Executive::executeCreate(Address const& _sender, u256 const& _endowment, u256 const& _gasPrice,
    u256 const& _gas, bytesConstRef _init, Address const& _origin)
{
    // check authority for deploy contract
    auto memoryTableFactory = m_envInfo.precompiledEngine()->getMemoryTableFactory();
    auto table = memoryTableFactory->openTable(SYS_TABLES);
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

    if (g_BCOSConfig.version() >= V2_5_0 && m_s->frozen(_origin))
    {
        LOG(DEBUG) << LOG_DESC("deploy contract failed for account frozen")
                   << LOG_KV("account", _origin);
        writeErrInfoToOutput("Frozen account:0x" + _origin.hex());
        m_gas = 0;
        revert();
        m_excepted = TransactionException::AccountFrozen;
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

    if (g_BCOSConfig.version() >= V2_3_0)
    {
        grantContractStatusManager(memoryTableFactory, m_newAddress, _sender, _origin);
    }

    // Schedule _init execution if not empty.
    if (!_init.empty())
    {
        m_ext = make_shared<EVMHostContext>(m_s, m_envInfo, m_newAddress, _sender, _origin,
            _endowment, _gasPrice, bytesConstRef(), _init.toBytes(), crypto::Hash(_init), m_depth,
            true, false);
        m_ext->setEvmFlags(m_evmFlags);
    }
    return !m_ext;
}

void Executive::grantContractStatusManager(TableFactory::Ptr memoryTableFactory,
    Address const& newAddress, Address const& sender, Address const& origin)
{
    LOG(DEBUG) << LOG_DESC("grantContractStatusManager") << LOG_KV("contract", newAddress)
               << LOG_KV("sender", sender) << LOG_KV("origin", origin);

    std::string tableName = precompiled::getContractTableName(newAddress);
    auto table = memoryTableFactory->openTable(tableName);

    if (!table)
    {
        LOG(ERROR) << LOG_DESC("grantContractStatusManager get newAddress table error!");
        return;
    }

    // grant origin authorization
    auto entry = table->newEntry();
    entry->setField("key", "authority");
    entry->setField("value", origin.hex());
    table->insert("authority", entry);
    LOG(DEBUG) << LOG_DESC("grantContractStatusManager add authoriy")
               << LOG_KV("origin", origin.hex());

    if (origin != sender)
    {
        // grant authorization of sender contract
        std::string senderTableName = precompiled::getContractTableName(sender);
        auto senderTable = memoryTableFactory->openTable(senderTableName);
        if (!senderTable)
        {
            LOG(ERROR) << LOG_DESC("grantContractStatusManager get sender table error!");
            return;
        }

        auto entries = senderTable->select("authority", senderTable->newCondition());
        if (entries->size() == 0)
        {
            LOG(ERROR) << LOG_DESC("grantContractStatusManager no sender authority is granted");
        }
        else
        {
            for (size_t i = 0; i < entries->size(); i++)
            {
                std::string authority = entries->get(i)->getField("value");
                if (origin.hex() != authority)
                {
                    // remove duplicate
                    auto entry = table->newEntry();
                    entry->setField("key", "authority");
                    entry->setField("value", authority);
                    table->insert("authority", entry);
                    LOG(DEBUG) << LOG_DESC("grantContractStatusManager add authoriy")
                               << LOG_KV("sender", authority);
                }
            }
        }
    }
    return;
}

bool Executive::go()
{
    if (m_ext)
    {
#if ETH_TIMED_EXECUTIONS
        Timer t;
#endif
        try
        {
            auto getEVMCMessage = [=]() -> shared_ptr<evmc_message> {
                // the block number will be larger than 0,
                // can be controlled by the programmers
                assert(m_ext->envInfo().number() >= 0);
                constexpr int64_t int64max = std::numeric_limits<int64_t>::max();
                if (m_gas > int64max || m_ext->envInfo().gasLimit() > int64max)
                {
                    LOG(ERROR) << LOG_DESC("Gas overflow") << LOG_KV("gas", m_gas)
                               << LOG_KV("gasLimit", m_ext->envInfo().gasLimit())
                               << LOG_KV("max gas/gasLimit", int64max);
                    BOOST_THROW_EXCEPTION(GasOverflow());
                }
                assert(m_ext->depth() <= static_cast<size_t>(std::numeric_limits<int32_t>::max()));
                evmc_call_kind kind = m_ext->isCreate() ? EVMC_CREATE : EVMC_CALL;
                uint32_t flags = m_ext->staticCall() ? EVMC_STATIC : 0;
                // this is ensured by solidity compiler
                assert(flags != EVMC_STATIC || kind == EVMC_CALL);  // STATIC implies a CALL.
                auto leftGas = static_cast<int64_t>(m_gas);
                return shared_ptr<evmc_message>(new evmc_message{toEvmC(m_ext->myAddress()),
                    toEvmC(m_ext->caller()), toEvmC(m_ext->value()), m_ext->data().data(),
                    m_ext->data().size(), toEvmC(m_ext->codeHash()), toEvmC(0x0_cppui256), leftGas,
                    static_cast<int32_t>(m_ext->depth()), kind, flags});
            };
            // Create VM instance. Force Interpreter if tracing requested.
            auto vm = VMFactory::create();
            if (m_isCreation)
            {
                m_s->clearStorage(m_ext->myAddress());
                auto mode = toRevision(m_ext->evmSchedule());
                auto emvcMessage = getEVMCMessage();
                auto ret = vm->exec(
                    *m_ext, mode, emvcMessage.get(), m_ext->code().data(), m_ext->code().size());
                parseEVMCResult(ret);
                auto outputRef = ret->output();
                if (outputRef.size() > m_ext->evmSchedule().maxCodeSize)
                {
                    m_exceptionReason << LOG_KV("reason", "Code is too long")
                                      << LOG_KV("size_limit", m_ext->evmSchedule().maxCodeSize)
                                      << LOG_KV("size", outputRef.size());
                    BOOST_THROW_EXCEPTION(OutOfGas());
                }
                else if (outputRef.size() * m_ext->evmSchedule().createDataGas <= m_gas)
                {
                    // When FreeStorage VM is enabled,
                    // the storage gas consumption of createData is not calculated additionally
                    if (!m_enableFreeStorage)
                    {
                        m_gas -= outputRef.size() * m_ext->evmSchedule().createDataGas;
                    }
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
                        outputRef = {};
                    }
                }
                m_s->setCode(m_ext->myAddress(),
                    bytes(outputRef.data(), outputRef.data() + outputRef.size()));
            }
            else
            {
                auto mode = toRevision(m_ext->evmSchedule());
                auto emvcMessage = getEVMCMessage();
                auto ret = vm->exec(
                    *m_ext, mode, emvcMessage.get(), m_ext->code().data(), m_ext->code().size());
                parseEVMCResult(ret);
            }
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

void Executive::parseEVMCResult(std::shared_ptr<eth::Result> _result)
{
    auto outputRef = _result->output();
    switch (_result->status())
    {
    case EVMC_SUCCESS:
    {
        m_gas = _result->gasLeft();
        m_output = owning_bytes_ref(
            bytes(outputRef.data(), outputRef.data() + outputRef.size()), 0, outputRef.size());
        break;
    }
    case EVMC_REVERT:
    {
        // FIXME: Copy the output for now, but copyless version possible.
        m_gas = _result->gasLeft();
        revert();
        m_output = owning_bytes_ref(
            bytes(outputRef.data(), outputRef.data() + outputRef.size()), 0, outputRef.size());
        m_excepted = TransactionException::RevertInstruction;
        break;
    }
    case EVMC_OUT_OF_GAS:
    case EVMC_FAILURE:
    {
        revert();
        m_excepted = TransactionException::OutOfGas;
        break;
    }

    case EVMC_INVALID_INSTRUCTION:  // NOTE: this could have its own exception
    case EVMC_UNDEFINED_INSTRUCTION:
    {
        m_gas = 0;
        m_excepted = TransactionException::BadInstruction;
        revert();
        break;
    }

    case EVMC_BAD_JUMP_DESTINATION:
    {
        m_gas = 0;
        m_excepted = TransactionException::BadJumpDestination;
        revert();
        break;
    }
    case EVMC_STACK_OVERFLOW:
    {
        m_gas = 0;
        m_excepted = TransactionException::OutOfStack;
        revert();
        break;
    }
    case EVMC_STACK_UNDERFLOW:
    {
        m_gas = 0;
        m_excepted = TransactionException::StackUnderflow;
        revert();
        break;
    }
    case EVMC_INVALID_MEMORY_ACCESS:
    {
        m_gas = 0;
        LOG(WARNING) << LOG_DESC("VM error, BufferOverrun");
        m_excepted = TransactionException::Unknown;
        revert();
        // BOOST_THROW_EXCEPTION(BufferOverrun());
        break;
    }
    case EVMC_STATIC_MODE_VIOLATION:
    {
        m_gas = 0;
        LOG(WARNING) << LOG_DESC("VM error, DisallowedStateChange");
        m_excepted = TransactionException::Unknown;
        revert();
        // BOOST_THROW_EXCEPTION(DisallowedStateChange());
        break;
    }
    case EVMC_INTERNAL_ERROR:
    default:
    {
        if (_result->status() <= EVMC_INTERNAL_ERROR)
        {
            BOOST_THROW_EXCEPTION(InternalVMError{} << errinfo_evmcStatusCode(_result->status()));
        }
        else
        {  // These cases aren't really internal errors, just more specific error codes returned by
           // the VM. Map all of them to OOG.
            BOOST_THROW_EXCEPTION(OutOfGas());
        }
    }
    }
}

void Executive::loggingException()
{
    if (m_excepted != TransactionException::None)
    {
        LOG(ERROR) << LOG_BADGE("TxExeError") << LOG_DESC("Transaction execution error")
                   << LOG_KV("TransactionExceptionID", (uint32_t)m_excepted)
                   << LOG_KV("hash", (m_t->hasSignature()) ? toHex(m_t->sha3()) : "call")
                   << m_exceptionReason.str();
    }
}

void Executive::writeErrInfoToOutput(string const& errInfo)
{
    eth::ContractABI abi;
    auto output = abi.abiIn("Error(string)", errInfo);
    m_output = owning_bytes_ref{std::move(output), 0, output.size()};
}