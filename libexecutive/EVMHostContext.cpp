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
/**
 * @Legacy EVM context
 *
 * @file EVMHostContext.cpp
 * @author jimmyshi
 * @date 2018-09-22
 */

#include "EVMHostContext.h"
#include "EVMHostInterface.h"
#include "evmc/evmc.hpp"
#include "libstorage/MemoryTableFactory2.h"
#include <libblockverifier/ExecutiveContext.h>
#include <boost/algorithm/string/split.hpp>
#include <boost/thread.hpp>
#include <algorithm>
#include <exception>
#include <limits>
#include <sstream>
#include <vector>

using namespace std;
using namespace dev;
using namespace dev::eth;
using namespace dev::executive;
using namespace dev::storage;

namespace  // anonymous
{
static unsigned const c_depthLimit = 1024;

/// Upper bound of stack space needed by single CALL/CREATE execution. Set experimentally.
static size_t const c_singleExecutionStackSize = 100 * 1024;

/// Standard thread stack size.
static size_t const c_defaultStackSize =
#if defined(__linux)
    8 * 1024 * 1024;
#elif defined(_WIN32)
    16 * 1024 * 1024;
#else
    512 * 1024;  // OSX and other OSs
#endif

/// Stack overhead prior to allocation.
static size_t const c_entryOverhead = 128 * 1024;

/// On what depth execution should be offloaded to additional separated stack space.
static unsigned const c_offloadPoint =
    (c_defaultStackSize - c_entryOverhead) / c_singleExecutionStackSize;

void goOnOffloadedStack(Executive& _e)
{
    // Set new stack size enouth to handle the rest of the calls up to the limit.
    boost::thread::attributes attrs;
    attrs.set_stack_size((c_depthLimit - c_offloadPoint) * c_singleExecutionStackSize);

    // Create new thread with big stack and join immediately.
    // TODO: It is possible to switch the implementation to Boost.Context or similar when the API is
    // stable.
    boost::exception_ptr exception;
    boost::thread{attrs,
        [&] {
            try
            {
                _e.go();
            }
            catch (...)
            {
                exception = boost::current_exception();  // Catch all exceptions to be rethrown in
                                                         // parent thread.
            }
        }}
        .join();
    if (exception)
        boost::rethrow_exception(exception);
}

void go(unsigned _depth, Executive& _e)
{
    // If in the offloading point we need to switch to additional separated stack space.
    // Current stack is too small to handle more CALL/CREATE executions.
    // It needs to be done only once as newly allocated stack space it enough to handle
    // the rest of the calls up to the depth limit (c_depthLimit).

    if (_depth == c_offloadPoint)
    {
        EXECUTIVE_LOG(TRACE) << "Stack offloading (depth: " << c_offloadPoint << ")";
        goOnOffloadedStack(_e);
    }
    else
        _e.go();
}

void generateCallResult(
    evmc_result* o_result, evmc_status_code status, u256 gas, owning_bytes_ref&& output)
{
    o_result->status_code = status;
    o_result->gas_left = static_cast<int64_t>(gas);

    // Pass the output to the EVM without a copy. The EVM will delete it
    // when finished with it.

    // First assign reference. References are not invalidated when vector
    // of bytes is moved. See `.takeBytes()` below.
    o_result->output_data = output.data();
    o_result->output_size = output.size();

    // Place a new vector of bytes containing output in result's reserved memory.
    auto* data = evmc_get_optional_storage(o_result);
    static_assert(sizeof(bytes) <= sizeof(*data), "Vector is too big");
    new (data) bytes(output.takeBytes());
    // Set the destructor to delete the vector.
    o_result->release = [](evmc_result const* _result) {
        // check _result is not null
        if (_result == NULL)
        {
            return;
        }
        auto* data = evmc_get_const_optional_storage(_result);
        auto& output = reinterpret_cast<bytes const&>(*data);
        // Explicitly call vector's destructor to release its data.
        // This is normal pattern when placement new operator is used.
        output.~bytes();
    };
}

void generateCreateResult(evmc_result* o_result, evmc_status_code status, u256 gas,
    owning_bytes_ref&& output, Address const& address)
{
    if (status == EVMC_SUCCESS)
    {
        o_result->status_code = status;
        o_result->gas_left = static_cast<int64_t>(gas);
        o_result->release = nullptr;
        o_result->create_address = toEvmC(address);
        o_result->output_data = nullptr;
        o_result->output_size = 0;
    }
    else
        generateCallResult(o_result, status, gas, std::move(output));
}

evmc_status_code transactionExceptionToEvmcStatusCode(TransactionException ex) noexcept
{
    switch (ex)
    {
    case TransactionException::None:
        return EVMC_SUCCESS;

    case TransactionException::RevertInstruction:
        return EVMC_REVERT;

    case TransactionException::OutOfGas:
        return EVMC_OUT_OF_GAS;

    case TransactionException::BadInstruction:
        return EVMC_UNDEFINED_INSTRUCTION;

    case TransactionException::OutOfStack:
        return EVMC_STACK_OVERFLOW;

    case TransactionException::StackUnderflow:
        return EVMC_STACK_UNDERFLOW;

    case TransactionException ::BadJumpDestination:
        return EVMC_BAD_JUMP_DESTINATION;

    default:
        return EVMC_FAILURE;
    }
}

}  // anonymous namespace

namespace dev
{
namespace executive
{
evmc_bytes32 sm3Hash(const uint8_t* data, size_t size)
{
    evmc_bytes32 hash;
    sm3(data, size, hash.bytes);
    return hash;
}

evmc_gas_metrics ethMetrics{32000, 20000, 5000, 200, 9000, 2300, 25000};
evmc_gas_metrics freeStorageGasMetrics{16000, 1200, 1200, 1200, 0, 5, 5};

EVMHostContext::EVMHostContext(std::shared_ptr<StateFace> _s,
    dev::executive::EnvInfo const& _envInfo, Address const& _myAddress, Address const& _caller,
    Address const& _origin, u256 const& _value, u256 const& _gasPrice, bytesConstRef _data,
    const bytes& _code, h256 const& _codeHash, unsigned _depth, bool _isCreate, bool _staticCall,
    bool _freeStorage)
  : m_envInfo(_envInfo),
    m_myAddress(_myAddress),
    m_caller(_caller),
    m_origin(_origin),
    m_value(_value),
    m_gasPrice(_gasPrice),
    m_data(_data),
    m_code(_code),
    m_codeHash(_codeHash),
    m_depth(_depth),
    m_isCreate(_isCreate),
    m_staticCall(_staticCall),
    m_freeStorage(_freeStorage),
    m_s(_s)
{
    m_memoryTableFactory = m_envInfo.precompiledEngine()->getMemoryTableFactory();
    interface = getHostInterface();
    sm3_hash_fn = nullptr;
    if (g_BCOSConfig.SMCrypto())
    {
        sm3_hash_fn = sm3Hash;
    }
    version = g_BCOSConfig.version();
    if (m_freeStorage)
    {
        metrics = &freeStorageGasMetrics;
    }
    else
    {
        metrics = &ethMetrics;
    }
}

evmc_result EVMHostContext::call(CallParameters& _p)
{
    Executive e{m_s, envInfo(), m_freeStorage, depth() + 1};
    stringstream ss;
    // Note: When create initializes Executive, the flags of evmc context must be passed in
    if (!e.call(_p, gasPrice(), origin()))
    {
        go(depth(), e);
        e.accrueSubState(sub());
    }
    _p.gas = e.gas();

    evmc_result evmcResult;
    generateCallResult(&evmcResult, transactionExceptionToEvmcStatusCode(e.getException()), _p.gas,
        e.takeOutput());
    return evmcResult;
}

size_t EVMHostContext::codeSizeAt(dev::Address const& _a)
{
    if (m_envInfo.precompiledEngine()->isPrecompiled(_a))
    {
        return 1;
    }
    return m_s->codeSize(_a);
}

h256 EVMHostContext::codeHashAt(Address const& _a)
{
    return exists(_a) ? m_s->codeHash(_a) : h256{};
}

bool EVMHostContext::isPermitted()
{
    // check authority by tx.origin
    if (!m_s->checkAuthority(origin(), myAddress()))
    {
        EXECUTIVE_LOG(ERROR) << "isPermitted PermissionDenied" << LOG_KV("origin", origin())
                             << LOG_KV("address", myAddress());
        return false;
    }
    return true;
}

void EVMHostContext::setStore(u256 const& _n, u256 const& _v)
{
    m_s->setStorage(myAddress(), _n, _v);
}

evmc_result EVMHostContext::create(
    u256 const& _endowment, u256& io_gas, bytesConstRef _code, evmc_opcode _op, u256 _salt)
{
    Executive e{m_s, envInfo(), m_freeStorage, depth() + 1};
    // Note: When create initializes Executive, the flags of evmc context must be passed in
    bool result = false;
    if (_op == evmc_opcode::OP_CREATE)
        result = e.createOpcode(myAddress(), _endowment, gasPrice(), io_gas, _code, origin());
    else
    {
        // TODO: when new CREATE opcode added, this logic maybe affected
        assert(_op == evmc_opcode::OP_CREATE2);
        result =
            e.create2Opcode(myAddress(), _endowment, gasPrice(), io_gas, _code, origin(), _salt);
    }

    if (!result)
    {
        go(depth(), e);
        e.accrueSubState(sub());
    }
    io_gas = e.gas();
    evmc_result evmcResult;
    generateCreateResult(&evmcResult, transactionExceptionToEvmcStatusCode(e.getException()),
        io_gas, e.takeOutput(), e.newAddress());
    return evmcResult;
}

void EVMHostContext::suicide(Address const& _a)
{
    // Why transfer is not used here? That caused a consensus issue before (see Quirk #2 in
    // http://martin.swende.se/blog/Ethereum_quirks_and_vulns.html). There is one test case
    // witnessing the current consensus
    // 'GeneralStateTests/stSystemOperationsTest/suicideSendEtherPostDeath.json'.

    if (g_BCOSConfig.version() >= RC2_VERSION)
    {
        // No balance here in BCOS. Balance has data racing in parallel suicide.
        m_sub.suicides.insert(m_myAddress);
        return;
    }

    m_s->addBalance(_a, m_s->balance(myAddress()));
    m_s->setBalance(myAddress(), 0);
    m_sub.suicides.insert(m_myAddress);
}

h256 EVMHostContext::blockHash(int64_t _number)
{
    return envInfo().numberHash(_number);
}

bool EVMHostContext::registerAsset(const std::string& _assetName, Address const& _addr,
    bool _fungible, uint64_t _total, const std::string& _description)
{
    auto table = m_memoryTableFactory->openTable(SYS_ASSET_INFO, false);
    auto entries = table->select(_assetName, table->newCondition());
    if (entries->size() != 0)
    {
        return false;
    }
    auto entry = table->newEntry();
    entry->setField(SYS_ASSET_NAME, _assetName);
    entry->setField(SYS_ASSET_ISSUER, _addr.hexPrefixed());
    entry->setField(SYS_ASSET_FUNGIBLE, to_string(_fungible));
    entry->setField(SYS_ASSET_TOTAL, to_string(_total));
    entry->setField(SYS_ASSET_SUPPLIED, "0");
    entry->setField(SYS_ASSET_DESCRIPTION, _description);
    auto count = table->insert(_assetName, entry);
    return count == 1;
}

bool EVMHostContext::issueFungibleAsset(
    Address const& _to, const std::string& _assetName, uint64_t _amount)
{
    auto table = m_memoryTableFactory->openTable(SYS_ASSET_INFO, false);
    auto entries = table->select(_assetName, table->newCondition());
    if (entries->size() != 1)
    {
        EXECUTIVE_LOG(WARNING) << "issueFungibleAsset " << _assetName << "is not exist";
        return false;
    }
    auto entry = entries->get(0);
    auto issuer = Address(entry->getField(SYS_ASSET_ISSUER));
    if (caller() != issuer)
    {
        EXECUTIVE_LOG(WARNING) << "issueFungibleAsset not issuer of " << _assetName
                               << LOG_KV("issuer", issuer) << LOG_KV("caller", caller());
        return false;
    }
    // TODO: check supplied is less than total_supply
    auto total = boost::lexical_cast<uint64_t>(entry->getField(SYS_ASSET_TOTAL));
    auto supplied = boost::lexical_cast<uint64_t>(entry->getField(SYS_ASSET_SUPPLIED));
    if (total - supplied < _amount)
    {
        EXECUTIVE_LOG(WARNING) << "issueFungibleAsset overflow total supply";
        return false;
    }
    // TODO: update supplied
    auto updateEntry = table->newEntry();
    updateEntry->setField(SYS_ASSET_SUPPLIED, to_string(supplied + _amount));
    table->update(_assetName, updateEntry, table->newCondition());
    // TODO: create new tokens
    depositFungibleAsset(_to, _assetName, _amount);
    return true;
}

uint64_t EVMHostContext::issueNotFungibleAsset(
    Address const& _to, const std::string& _assetName, const std::string& _uri)
{
    // check issuer
    auto table = m_memoryTableFactory->openTable(SYS_ASSET_INFO, false);
    auto entries = table->select(_assetName, table->newCondition());
    if (entries->size() != 1)
    {
        EXECUTIVE_LOG(WARNING) << "issueNotFungibleAsset " << _assetName << "is not exist";
        return false;
    }
    auto entry = entries->get(0);
    auto issuer = Address(entry->getField(SYS_ASSET_ISSUER));
    if (caller() != issuer)
    {
        EXECUTIVE_LOG(WARNING) << "issueNotFungibleAsset not issuer of " << _assetName;
        return false;
    }
    // check supplied
    auto total = boost::lexical_cast<uint64_t>(entry->getField(SYS_ASSET_TOTAL));
    auto supplied = boost::lexical_cast<uint64_t>(entry->getField(SYS_ASSET_SUPPLIED));
    if (total - supplied == 0)
    {
        EXECUTIVE_LOG(WARNING) << "issueNotFungibleAsset overflow total supply";
        return false;
    }
    // get asset id and update supplied
    auto assetID = supplied + 1;
    auto updateEntry = table->newEntry();
    updateEntry->setField(SYS_ASSET_SUPPLIED, to_string(assetID));
    table->update(_assetName, updateEntry, table->newCondition());

    // create new tokens
    depositNotFungibleAsset(_to, _assetName, assetID, _uri);
    return assetID;
}

void EVMHostContext::depositFungibleAsset(
    Address const& _to, const std::string& _assetName, uint64_t _amount)
{
    auto tableName = "c_" + _to.hex();
    auto table = m_memoryTableFactory->openTable(tableName, false);
    if (!table)
    {
        EXECUTIVE_LOG(DEBUG) << LOG_DESC("depositFungibleAsset createAccount")
                             << LOG_KV("account", _to.hex());
        m_s->setNonce(_to, u256(0));
        table = m_memoryTableFactory->openTable(tableName, false);
    }
    auto entries = table->select(_assetName, table->newCondition());
    if (entries->size() == 0)
    {
        auto entry = table->newEntry();
        entry->setField("key", _assetName);
        entry->setField("value", to_string(_amount));
        table->insert(_assetName, entry);
        return;
    }
    auto entry = entries->get(0);
    auto value = boost::lexical_cast<uint64_t>(entry->getField("value"));
    value += _amount;
    auto updateEntry = table->newEntry();
    updateEntry->setField("value", to_string(value));
    table->update(_assetName, updateEntry, table->newCondition());
}

void EVMHostContext::depositNotFungibleAsset(
    Address const& _to, const std::string& _assetName, uint64_t _assetID, const std::string& _uri)
{
    auto tableName = "c_" + _to.hex();
    auto table = m_memoryTableFactory->openTable(tableName, false);
    if (!table)
    {
        EXECUTIVE_LOG(DEBUG) << LOG_DESC("depositNotFungibleAsset createAccount")
                             << LOG_KV("account", _to.hex());
        m_s->setNonce(_to, u256(0));
        table = m_memoryTableFactory->openTable(tableName, false);
    }
    auto entries = table->select(_assetName, table->newCondition());
    if (entries->size() == 0)
    {
        auto entry = table->newEntry();
        entry->setField("value", to_string(_assetID));
        entry->setField("key", _assetName);
        table->insert(_assetName, entry);
    }
    else
    {
        auto entry = entries->get(0);
        auto assetIDs = entry->getField("value");
        if (assetIDs.empty())
        {
            assetIDs = to_string(_assetID);
        }
        else
        {
            assetIDs = assetIDs + "," + to_string(_assetID);
        }
        auto updateEntry = table->newEntry();
        updateEntry->setField("key", _assetName);
        updateEntry->setField("value", assetIDs);
        table->update(_assetName, updateEntry, table->newCondition());
    }
    auto entry = table->newEntry();
    auto key = _assetName + "-" + to_string(_assetID);
    entry->setField("key", key);
    entry->setField("value", _uri);
    table->insert(key, entry);
}

bool EVMHostContext::transferAsset(
    Address const& _to, const std::string& _assetName, uint64_t _amountOrID, bool _fromSelf)
{
    // get asset info
    auto table = m_memoryTableFactory->openTable(SYS_ASSET_INFO, false);
    auto entries = table->select(_assetName, table->newCondition());
    if (entries->size() != 1)
    {
        EXECUTIVE_LOG(WARNING) << "transferAsset " << _assetName << " is not exist";
        return false;
    }
    auto assetEntry = entries->get(0);
    auto fungible = boost::lexical_cast<bool>(assetEntry->getField(SYS_ASSET_FUNGIBLE));
    auto from = caller();
    if (_fromSelf)
    {
        from = myAddress();
    }
    auto tableName = "c_" + from.hex();
    table = m_memoryTableFactory->openTable(tableName, false);
    entries = table->select(_assetName, table->newCondition());
    if (entries->size() == 0)
    {
        EXECUTIVE_LOG(WARNING) << LOG_DESC("transferAsset account does not have")
                               << LOG_KV("asset", _assetName)
                               << LOG_KV("account", from.hexPrefixed());
        return false;
    }
    EXECUTIVE_LOG(DEBUG) << LOG_DESC("transferAsset") << LOG_KV("asset", _assetName)
                         << LOG_KV("fungible", fungible) << LOG_KV("account", from.hexPrefixed());
    try
    {
        if (fungible)
        {
            auto entry = entries->get(0);
            auto value = boost::lexical_cast<uint64_t>(entry->getField("value"));
            value -= _amountOrID;
            auto updateEntry = table->newEntry();
            updateEntry->setField("key", _assetName);
            updateEntry->setField("value", to_string(value));
            table->update(_assetName, updateEntry, table->newCondition());
            depositFungibleAsset(_to, _assetName, _amountOrID);
        }
        else
        {
            // TODO: check if from has asset
            auto entry = entries->get(0);
            auto tokenIDs = entry->getField("value");
            // find id in tokenIDs
            auto tokenID = to_string(_amountOrID);
            vector<string> tokenIDList;
            boost::split(tokenIDList, tokenIDs, boost::is_any_of(","));
            auto it = find(tokenIDList.begin(), tokenIDList.end(), tokenID);
            if (it != tokenIDList.end())
            {
                tokenIDList.erase(it);
                auto updateEntry = table->newEntry();
                updateEntry->setField("value", boost::algorithm::join(tokenIDList, ","));
                table->update(_assetName, updateEntry, table->newCondition());
                auto tokenKey = _assetName + "-" + tokenID;
                entries = table->select(tokenKey, table->newCondition());
                entry = entries->get(0);
                auto tokenURI = entry->getField("value");
                table->remove(tokenKey, table->newCondition());
                depositNotFungibleAsset(_to, _assetName, _amountOrID, tokenURI);
            }
            else
            {
                EXECUTIVE_LOG(WARNING)
                    << LOG_DESC("transferAsset account does not have")
                    << LOG_KV("asset", _assetName) << LOG_KV("account", from.hexPrefixed());
                return false;
            }
        }
    }
    catch (std::exception& e)
    {
        EXECUTIVE_LOG(ERROR) << "transferAsset exception" << LOG_KV("what", e.what());
        return false;
    }

    return true;
}

uint64_t EVMHostContext::getAssetBanlance(Address const& _account, const std::string& _assetName)
{
    auto table = m_memoryTableFactory->openTable(SYS_ASSET_INFO, false);
    auto entries = table->select(_assetName, table->newCondition());
    if (entries->size() != 1)
    {
        EXECUTIVE_LOG(WARNING) << "getAssetBanlance " << _assetName << " is not exist";
        return false;
    }
    auto assetEntry = entries->get(0);
    auto fungible = boost::lexical_cast<bool>(assetEntry->getField(SYS_ASSET_FUNGIBLE));
    auto tableName = "c_" + _account.hex();
    table = m_memoryTableFactory->openTable(tableName, false);
    if (!table)
    {
        return 0;
    }
    entries = table->select(_assetName, table->newCondition());
    if (entries->size() == 0)
    {
        return 0;
    }
    auto entry = entries->get(0);
    if (fungible)
    {
        return boost::lexical_cast<uint64_t>(entry->getField("value"));
    }
    // not fungible
    auto tokenIDS = entry->getField("value");
    uint64_t counts = std::count(tokenIDS.begin(), tokenIDS.end(), ',') + 1;
    return counts;
}

std::string EVMHostContext::getNotFungibleAssetInfo(
    Address const& _owner, const std::string& _assetName, uint64_t _assetID)
{
    auto tableName = "c_" + _owner.hex();
    auto table = m_memoryTableFactory->openTable(tableName, false);
    if (!table)
    {
        EXECUTIVE_LOG(WARNING) << "getNotFungibleAssetInfo failed, account not exist"
                               << LOG_KV("account", _owner.hex());
        return "";
    }
    auto assetKey = _assetName + "-" + to_string(_assetID);
    auto entries = table->select(assetKey, table->newCondition());
    if (entries->size() == 0)
    {
        EXECUTIVE_LOG(WARNING) << "getNotFungibleAssetInfo failed"
                               << LOG_KV("account", _owner.hex()) << LOG_KV("asset", assetKey);
        return "";
    }
    auto entry = entries->get(0);
    EXECUTIVE_LOG(DEBUG) << "getNotFungibleAssetInfo" << LOG_KV("account", _owner.hex())
                         << LOG_KV("asset", _assetName) << LOG_KV("uri", entry->getField("value"));
    return entry->getField("value");
}

std::vector<uint64_t> EVMHostContext::getNotFungibleAssetIDs(
    Address const& _account, const std::string& _assetName)
{
    auto tableName = "c_" + _account.hex();
    auto table = m_memoryTableFactory->openTable(tableName, false);
    if (!table)
    {
        EXECUTIVE_LOG(WARNING) << "getNotFungibleAssetIDs account not exist"
                               << LOG_KV("account", _account.hex());
        return vector<uint64_t>();
    }
    auto entries = table->select(_assetName, table->newCondition());
    auto entry = entries->get(0);
    auto tokenIDs = entry->getField("value");
    if (tokenIDs.empty())
    {
        EXECUTIVE_LOG(WARNING) << "getNotFungibleAssetIDs account has none asset"
                               << LOG_KV("account", _account.hex()) << LOG_KV("asset", _assetName);
        return vector<uint64_t>();
    }
    vector<string> tokenIDList;
    boost::split(tokenIDList, tokenIDs, boost::is_any_of(","));
    vector<uint64_t> ret(tokenIDList.size(), 0);
    EXECUTIVE_LOG(DEBUG) << "getNotFungibleAssetIDs" << LOG_KV("account", _account.hex())
                         << LOG_KV("asset", _assetName) << LOG_KV("tokenIDs", tokenIDs);
    for (size_t i = 0; i < tokenIDList.size(); ++i)
    {
        ret[i] = boost::lexical_cast<uint64_t>(tokenIDList[i]);
    }
    return ret;
}
}  // namespace executive
}  // namespace dev