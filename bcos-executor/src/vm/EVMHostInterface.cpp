/*
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @brief host context
 * @file EVMHostInterface.cpp
 * @author: xingqiangbai
 * @date: 2021-05-24
 * @brief host context
 * @file EVMHostInterface.cpp
 * @author: ancelmo
 * @date: 2021-09-13
 */

// NOTE: evmc_host_context is upstream-opaque; every context pointer entering these
// shims is the executor's own HostContext (see VMInstance), so the reinterpret_casts
// below are the inverse of the cast made at the execute() call sites.

#include "EVMHostInterface.h"
#include "../Common.h"
#include "HostContext.h"
#include "bcos-utilities/Common.h"
#include <evmc/evmc.h>
#include <boost/algorithm/hex.hpp>
#include <boost/core/ignore_unused.hpp>
#include <limits>

using namespace std;

namespace bcos
{
namespace executor
{
namespace
{
static_assert(sizeof(Address) == sizeof(evmc_address), "Address types size mismatch");
static_assert(alignof(Address) == alignof(evmc_address), "Address types alignment mismatch");
static_assert(sizeof(h256) == sizeof(evmc_bytes32), "Hash types size mismatch");
static_assert(alignof(h256) == alignof(evmc_bytes32), "Hash types alignment mismatch");

bool accountExists(evmc_host_context* _context, const evmc_address* _addr) noexcept
{
    auto& hostContext = *reinterpret_cast<HostContext*>(_context);
    auto addr = fromEvmC(*_addr);
    return hostContext.exists(addr);
}

evmc_bytes32 getStorage(
    evmc_host_context* context, [[maybe_unused]] const evmc_address* addr, const evmc_bytes32* key)
{
    auto& hostContext = *reinterpret_cast<HostContext*>(context);

    // programming assert for debug
    assert(fromEvmC(*addr) == boost::algorithm::unhex(std::string(hostContext.myAddress())));

    return hostContext.store(key);
}

evmc_bytes32 getTransientStorage(
    evmc_host_context* context, [[maybe_unused]] const evmc_address* addr, const evmc_bytes32* key)
{
    auto& hostContext = *reinterpret_cast<HostContext*>(context);

    // programming assert for debug
    assert(fromEvmC(*addr) == boost::algorithm::unhex(std::string(hostContext.myAddress())));

    return hostContext.getTransientStorage(key);
}


evmc_storage_status setStorage(evmc_host_context* context,
    [[maybe_unused]] const evmc_address* addr, const evmc_bytes32* key, const evmc_bytes32* value)
{
    auto& hostContext = *reinterpret_cast<HostContext*>(context);

    assert(fromEvmC(*addr) == boost::algorithm::unhex(std::string(hostContext.myAddress())));

    evmc_storage_status status;
    if (hostContext.features().get(ledger::Features::Flag::bugfix_evm_storage_status))
    {
        // TODO: full EIP-2200 support — also report the 5 dirty-slot statuses
        // (DELETED_ADDED, MODIFIED_DELETED, DELETED_RESTORED, ADDED_DELETED,
        // MODIFIED_RESTORED). Requires tracking the transaction-original value
        // per slot to distinguish clean (o == c) from dirty (o != c) writes.
        const auto existingValue = hostContext.store(key);
        const bool existingIsZero =
            concepts::bytebuffer::equalTo(existingValue.bytes, EMPTY_EVM_BYTES32.bytes);
        if (concepts::bytebuffer::equalTo(value->bytes, EMPTY_EVM_BYTES32.bytes))
        {
            status = existingIsZero ? EVMC_STORAGE_ASSIGNED : EVMC_STORAGE_DELETED;
        }
        else
        {
            status = existingIsZero ? EVMC_STORAGE_ADDED : EVMC_STORAGE_MODIFIED;
        }
        // evmone applies gas_refund from the returned status; no manual refund bookkeeping.
    }
    else
    {
        // Legacy behavior preserved verbatim for consensus compatibility.
        status = EVMC_STORAGE_MODIFIED;
        if (value == 0)  // historical: pointer compared against 0; effectively never true
        {
            status = EVMC_STORAGE_DELETED;
            hostContext.sub().refunds += hostContext.vmSchedule().sstoreRefundGas;
        }
    }
    hostContext.setStore(key, value);  // Interface uses native endianness
    return status;
}

void setTransientStorage(evmc_host_context* context, [[maybe_unused]] const evmc_address* addr,
    const evmc_bytes32* key, const evmc_bytes32* value)
{
    auto& hostContext = *reinterpret_cast<HostContext*>(context);
    hostContext.setTransientStorage(key, value);  // Interface uses native endianness
}


evmc_bytes32 getBalance(evmc_host_context* _context, const evmc_address* _addr) noexcept
{
    auto& hostContext = *reinterpret_cast<HostContext*>(_context);
    const auto& blockContext = hostContext.getTransactionExecutive()->blockContext();
    EXECUTIVE_LOG(DEBUG) << "start getBalance in evm";
    if (blockContext.features().get(ledger::Features::Flag::feature_balance))
    {
        return hostContext.getBalance(_addr);
    }
    else
    {
        // always return 0
        (void)_context;
        (void)_addr;
        return toEvmC(h256(0));
    }
}

// evmc ABI 19: the VM reads the sender's nonce itself to compute CREATE addresses
// (evmone instructions_calls.cpp create_impl) and to enforce EIP-2681; the value
// must match the nonce the executive reads when it assigns the created address.
uint64_t getNonce(evmc_host_context* _context, const evmc_address* _addr) noexcept
{
    auto& hostContext = *reinterpret_cast<HostContext*>(_context);
    try
    {
        return hostContext.getNonce(_addr);
    }
    catch (...)
    {
        // The body reads storage and parses the stored nonce string, both of which can
        // throw; an exception escaping this noexcept callback is std::terminate. Fail
        // closed per EIP-2681 instead: MAX_NONCE makes evmone reject the CREATE.
        return std::numeric_limits<uint64_t>::max();
    }
}

size_t getCodeSize(evmc_host_context* _context, const evmc_address* _addr)
{
    auto& hostContext = *reinterpret_cast<HostContext*>(_context);
    return hostContext.codeSizeAt(fromEvmC(*_addr));
}

evmc_bytes32 getCodeHash(evmc_host_context* _context, const evmc_address* _addr)
{
    auto& hostContext = *reinterpret_cast<HostContext*>(_context);
    return toEvmC(hostContext.codeHashAt(fromEvmC(*_addr)));
}

/**
 * @brief : copy code between [_codeOffset, _codeOffset + _bufferSize] to
 * bufferData if _codeOffset is larger than code length, then return 0; if
 * _codeOffset + _bufferSize is larger than the end of the code, than only copy
 * [_codeOffset, _codeEnd]
 * @param _context : evm context, including to myAddress, caller, gas, origin,
 * value, etc.
 * @param _addr: the evmc-address of the code
 * @param _codeOffset: the offset begin to copy code
 * @param _bufferData : buffer store the copied code
 * @param _bufferSize : code size to copy
 * @return size_t : return copied code size(in byte)
 */
size_t copyCode(evmc_host_context* _context, const evmc_address* _addr, size_t _codeOffset,
    uint8_t* _bufferData, size_t _bufferSize)
{
    auto& hostContext = *reinterpret_cast<HostContext*>(_context);
    if (hostContext.features().get(
            ledger::Features::Flag::bugfix_evm_create2_delegatecall_staticcall_codecopy))
    {
        auto addr = fromEvmC(*_addr);
        bytes const& code = hostContext.codeAt(addr);

        // Handle "big offset" edge case.
        if (_codeOffset >= code.size())
            return 0;

        size_t maxToCopy = code.size() - _codeOffset;
        size_t numToCopy = std::min(maxToCopy, _bufferSize);
        std::copy_n(&code[_codeOffset], numToCopy, _bufferData);
        return numToCopy;
    }

    hostContext.setCode(bytes((bcos::byte*)_bufferData, (bcos::byte*)_bufferData + _bufferSize));
    return _bufferSize;
}


bool selfdestruct(evmc_host_context* _context, const evmc_address* _addr,
    const evmc_address* _beneficiary) noexcept
{
    (void)_addr;
    (void)_beneficiary;
    auto& hostContext = *reinterpret_cast<HostContext*>(_context);
    const auto& blockContext = hostContext.getTransactionExecutive()->blockContext();
    EXECUTIVE_LOG(DEBUG) << "EVM suicide start ";
    if (blockContext.features().get(ledger::Features::Flag::feature_balance))
    {
        hostContext.selfdestruct(_addr, _beneficiary);
    }
    EXECUTIVE_LOG(DEBUG) << "selfdestruct successful";

    hostContext.suicide();  // FISCO BCOS has no _beneficiary
    // EIP-3529 (London): SELFDESTRUCT gas refund is removed entirely.
    // EIP-6780 (Cancun+): account deletion only when created in same tx;
    //   no gas refund in either case ("Note that no refund is given since EIP-3529").
    // Returning false (no refund) is the correct behavior for all cases.
    // TODO(evmone-eip6780): implement same-tx creation tracking for account deletion.
    return false;
}


void log(evmc_host_context* _context, const evmc_address* _addr, uint8_t const* _data,
    size_t _dataSize, const evmc_bytes32 _topics[], size_t _numTopics) noexcept
{
    (void)_addr;
    auto& hostContext = *reinterpret_cast<HostContext*>(_context);
    assert(fromEvmC(*_addr) == boost::algorithm::unhex(std::string(hostContext.myAddress())));
    h256 const* pTopics = reinterpret_cast<h256 const*>(_topics);
    hostContext.log(h256s{pTopics, pTopics + _numTopics}, bytesConstRef{_data, _dataSize});
}

evmc_access_status access_account(evmc_host_context* _context, const evmc_address* _addr)
{
    auto& hostContext = *reinterpret_cast<HostContext*>(_context);
    return hostContext.accessAccount(*_addr, hostContext.revision());
}


evmc_access_status access_storage(
    evmc_host_context* _context, const evmc_address* _addr, const evmc_bytes32* _key)
{
    auto& hostContext = *reinterpret_cast<HostContext*>(_context);
    return hostContext.accessStorage(*_addr, *_key, hostContext.revision());
}

evmc_tx_context getTxContext(evmc_host_context* _context) noexcept
{
    auto& hostContext = *reinterpret_cast<HostContext*>(_context);
    // FIXME: the evmc_tx_context should init use the transaction info
    evmc_tx_context result{};
    auto origin = fromHex(hostContext.origin());
    result.tx_origin = toEvmC(std::string_view((char*)origin.data(), origin.size()));
    result.block_number = hostContext.blockNumber();
    result.block_timestamp = hostContext.timestamp();
    result.block_gas_limit = hostContext.blockGasLimit();
    result.tx_gas_price = toEvmC(hostContext.gasPrice());
    result.chain_id = hostContext.chainId();

    // TODO(EIP-3651): set block_coinbase from blockHeader sealer (not zero); keep in sync with
    // Eip2929AccessState W1 warm-up. Ref: bcos-rpc/.../BlockResponse.cpp "miner" mapping.
    memset(result.block_coinbase.bytes, 0, 20);
    memset(result.block_prev_randao.bytes, 0, 32);
    memset(result.block_base_fee.bytes, 0, 32);


    return result;
}

evmc_bytes32 getBlockHash(evmc_host_context* _txContextPtr, int64_t _number)
{
    auto& hostContext = *reinterpret_cast<HostContext*>(_txContextPtr);
    return toEvmC(hostContext.blockHash(_number));
}

// evmc_result create(HostContext& _txContext, evmc_message const* _msg) noexcept
// {
//     return _txContext.externalRequest(_msg);
// int64_t gas = _msg->gas;
// // u256 value = fromEvmC(_msg->value);
// bytesConstRef init = {_msg->input_data, _msg->input_size};
// u256 salt = fromEvmC(_msg->create2_salt);
// evmc_opcode opcode =
//     _msg->kind == EVMC_CREATE ? evmc_opcode::OP_CREATE : evmc_opcode::OP_CREATE2;

// // HostContext::create takes the sender address from .myAddress().
// assert(fromEvmC(_msg->sender) == _txContext.myAddress());

// return _txContext.create(gas, init, opcode, salt);
// }

evmc_result call(evmc_host_context* _context, const evmc_message* _msg) noexcept
{
    // This callback is noexcept (the evmc host interface requires it): an exception
    // escaping it is std::terminate, so every throwing path inside must be translated
    // into a failed evmc_result — the same fail-closed shape getNonce below uses.
    try
    {
        // gas maybe smaller than 0 since outside gas is u256 and evmc_message is
        // int64_t so gas maybe smaller than 0 in some extreme cases
        // * origin code: assert(_msg->gas >= 0)
        if (_msg->gas < 0)
        {
            EXECUTIVE_LOG(INFO) << LOG_DESC("EVM Gas overflow") << LOG_KV("cur gas", _msg->gas);
            return evmc_result{.status_code = EVMC_OUT_OF_GAS, .gas_left = 0};
        }

        auto& hostContext = *reinterpret_cast<HostContext*>(_context);

        return hostContext.externalRequest(_msg);
    }
    catch (...)
    {
        // externalRequest reads storage and parses the stored nonce string (both can
        // throw); returning a failure lets evmone fail the create/call frame instead
        // of terminating the node mid-block.
        EXECUTIVE_LOG(WARNING) << LOG_DESC("EVM host call failed closed");
        return evmc_result{.status_code = EVMC_INTERNAL_ERROR, .gas_left = 0};
    }
}

/// function table
// clang-format off
// Designated initializers (C++20) keep each function pointer bound to its named
// slot: evmc ABI 13..19 inserted get_nonce mid-struct, which would silently
// shift every positional initializer that follows it.
evmc_host_interface const fnTable = {
    .account_exists = accountExists,
    .get_storage = getStorage,
    .set_storage = setStorage,
    .get_balance = getBalance,
    .get_nonce = getNonce,
    .get_code_size = getCodeSize,
    .get_code_hash = getCodeHash,
    .copy_code = copyCode,
    .selfdestruct = selfdestruct,
    .call = call,
    .get_tx_context = getTxContext,
    .get_block_hash = getBlockHash,
    .emit_log = log,
    .access_account = access_account,
    .access_storage = access_storage,
    .get_transient_storage = getTransientStorage,
    .set_transient_storage = setTransientStorage,
};
// clang-format on


}  // namespace

const evmc_host_interface* getHostInterface()
{
    return &fnTable;
}
}  // namespace executor
}  // namespace bcos