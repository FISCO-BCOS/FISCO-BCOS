/// @file BCOS2Evmone.h
/// @brief Type converters between BCOS protocol types and evmone state types.
#pragma once

#include "StorageStateView.h"
#include "bcos-evm/eth/state/state.hpp"
#include "bcos-evm/eth/state/transaction.hpp"
#include "bcos-framework/ledger/EVMAccount.h"
#include "bcos-framework/ledger/LedgerConfig.h"
#include "bcos-framework/protocol/BlockHeader.h"
#include "bcos-framework/protocol/LogEntry.h"
#include "bcos-framework/protocol/Transaction.h"
#include "bcos-framework/protocol/TransactionReceipt.h"
#include "bcos-framework/protocol/TransactionReceiptFactory.h"
#include "bcos-task/TBBWait.h"
#include <evmone/evmone.h>
#include <string>

namespace bcos::executor_v1::eth
{

using ::bcos::executor_v1::eth::toIntxU256;

inline evmone::state::BlockInfo blockHeaderToBlockInfo(
    protocol::BlockHeader const& header, ledger::LedgerConfig const& config)
{
    evmone::state::BlockInfo info{};
    info.number = header.number();
    info.timestamp = header.timestamp() / 1000L;
    info.gas_limit = static_cast<int64_t>(std::get<0>(config.gasLimit()));
    auto const& cb = header.coinbase();
    if (cb.size() == sizeof(evmc_address))
        std::copy_n(cb.begin(), sizeof(evmc_address), info.coinbase.bytes);
    auto baseFeeStr = std::get<0>(config.gasPrice());
    if (!baseFeeStr.empty() && baseFeeStr != "0x" && baseFeeStr != "0x0")
    {
        if (baseFeeStr.size() > 2 && baseFeeStr[0] == '0' && baseFeeStr[1] == 'x')
            baseFeeStr = baseFeeStr.substr(2);
        info.base_fee = static_cast<uint64_t>(std::stoull(baseFeeStr, nullptr, 16));
    }
    // blob_base_fee: for Cancun+, set to 1 (minimum, excessBlobGas=0).
    // EEST fixtures typically have excessBlobGas=0 so blob base fee = 1.
    info.blob_base_fee = intx::uint256{1};
    return info;
}

inline evmone::state::Transaction bcosTransactionToEvmone(
    protocol::Transaction const& tx)
{
    evmone::state::Transaction evmTx{};
    switch (tx.web3TypedTxKind())
    {
    case 0: evmTx.type = evmone::state::Transaction::Type::legacy; break;
    case 1: evmTx.type = evmone::state::Transaction::Type::access_list; break;
    case 2: evmTx.type = evmone::state::Transaction::Type::eip1559; break;
    case 3: evmTx.type = evmone::state::Transaction::Type::blob; break;
    case 4: evmTx.type = evmone::state::Transaction::Type::set_code; break;
    default: break;
    }
    auto const& input = tx.input();
    evmTx.data = evmc::bytes(input.begin(), input.end());
    evmTx.gas_limit = tx.gasLimit();
    if (auto gp = tx.gasPrice(); gp.has_value())
        evmTx.max_gas_price = toIntxU256(*gp);
    if (auto mf = tx.maxFeePerGas(); mf.has_value())
        evmTx.max_gas_price = toIntxU256(*mf);
    if (auto mp = tx.maxPriorityFeePerGas(); mp.has_value())
        evmTx.max_priority_gas_price = toIntxU256(*mp);
    if (auto mb = tx.maxFeePerBlobGas(); mb.has_value())
        evmTx.max_blob_gas_price = toIntxU256(*mb);

    // For legacy/access_list txs (no explicit maxPriorityFeePerGas),
    // set it = max_gas_price so coinbase gets the gas tip when base_fee=0.
    // Reference: evmone test/statetest/statetest_runner.cpp
    if ((evmTx.type == evmone::state::Transaction::Type::legacy ||
            evmTx.type == evmone::state::Transaction::Type::access_list) &&
        evmTx.max_priority_gas_price == 0)
        evmTx.max_priority_gas_price = evmTx.max_gas_price;
    auto const& sb = tx.sender();
    if (sb.size() >= sizeof(evmc_address))
        std::copy_n(sb.begin(), sizeof(evmc_address), evmTx.sender.bytes);
    auto const& tb = tx.to();
    if (!tb.empty() && tb.size() >= sizeof(evmc_address))
    {
        evmc_address ta{};
        std::copy_n(tb.begin(), sizeof(evmc_address), ta.bytes);
        evmTx.to = ta;
    }
    evmTx.value = toIntxU256(tx.value());
    for (auto const& entry : tx.web3AccessList())
    {
        evmc_address addr{};
        std::copy_n(entry.account.begin(), sizeof(evmc_address), addr.bytes);
        std::vector<evmc::bytes32> keys;
        for (auto const& sk : entry.storageKeys)
        {
            evmc_bytes32 key{};
            std::copy_n(sk.begin(), sizeof(evmc_bytes32), key.bytes);
            keys.push_back(key);
        }
        evmTx.access_list.emplace_back(addr, std::move(keys));
    }
    for (auto const& h : tx.blobVersionedHashes())
    {
        evmc_bytes32 hash{};
        std::copy_n(h.begin(), sizeof(evmc_bytes32), hash.bytes);
        evmTx.blob_hashes.push_back(hash);
    }
    // chainId and nonce from BCOS tx are stripped hex (no 0x prefix)
    auto cid = tx.chainId();
    if (!cid.empty())
        evmTx.chain_id = static_cast<uint64_t>(bcos::u256("0x" + std::string(cid)));
    auto nonceStr = std::string(tx.nonce());
    evmTx.nonce = static_cast<uint64_t>(
        nonceStr.empty() ? bcos::u256{} : bcos::u256("0x" + nonceStr));
    for (auto const& auth : tx.authorizationList())
    {
        evmone::state::Authorization ea{};
        // AuthorizationEntry: all fields are numeric (uint64_t, u256, Address, uint8_t)
        ea.chain_id = toIntxU256(bcos::u256(auth.chainId));
        std::copy_n(auth.address.begin(), sizeof(evmc_address), ea.addr.bytes);
        ea.nonce = auth.nonce;
        if (auth.signer.size() == sizeof(evmc_address))
        {
            evmc_address sa{};
            std::copy_n(auth.signer.begin(), sizeof(evmc_address), sa.bytes);
            ea.signer = sa;
        }
        ea.r = toIntxU256(auth.r);
        ea.s = toIntxU256(auth.s);
        ea.v = toIntxU256(bcos::u256(auth.v));
        evmTx.authorization_list.push_back(std::move(ea));
    }
    return evmTx;
}

// Forward declaration needed for applyStateDiff
inline bcos::u256 toBcosU256(intx::uint256 const& val)
{
    return bcos::u256(intx::to_string(val));
}

template <class Storage>
task::Task<void> applyStateDiff(Storage& storage, evmone::state::StateDiff const& diff,
    evmc_revision, crypto::Hash const& hashImpl)
{
    using namespace bcos::ledger::account;
    for (auto const& addr : diff.deleted_accounts)
    {
        EVMAccount<Storage> acc(storage, addr, false);
        if (co_await acc.exists())
        {
            co_await acc.setBalance(0);
            co_await acc.setNonce("0");
            co_await acc.setCode(bcos::bytes{}, std::string{}, bcos::h256{});
        }
    }
    for (auto const& m : diff.modified_accounts)
    {
        EVMAccount<Storage> acc(storage, m.addr, false);
        if (!co_await acc.exists())
            co_await acc.create();
        co_await acc.setNonce(std::to_string(m.nonce));
        co_await acc.setBalance(toBcosU256(m.balance));
        if (m.code.has_value())
        {
            auto const& c = *m.code;
            if (c.empty())
            {
                co_await acc.setCode(bcos::bytes{}, std::string{}, bcos::h256{});
            }
            else
            {
                bcos::bytes code(c.begin(), c.end());
                auto ch = hashImpl.hash(bcos::bytesConstRef(code.data(), code.size()));
                co_await acc.setCode(std::move(code), std::string{}, ch);
            }
        }
        for (auto const& [key, value] : m.modified_storage)
        {
            bool isZero = true;
            for (auto b : value.bytes) { if (b != 0) { isZero = false; break; } }
            if (isZero)
                co_await acc.setStorage(key, evmc_bytes32{});
            else
                co_await acc.setStorage(key, value);
        }
    }
}

inline protocol::TransactionReceipt::Ptr evmoneReceiptToBcos(
    evmone::state::TransactionReceipt const& er,
    protocol::TransactionReceiptFactory const& rf,
    int64_t blockNumber)
{
    std::vector<protocol::LogEntry> logs;
    for (auto const& l : er.logs)
    {
        bcos::bytes addr(l.addr.bytes, l.addr.bytes + sizeof(evmc_address));
        bcos::h256s topics;
        for (auto const& t : l.topics)
            topics.emplace_back(bcos::bytesConstRef(t.bytes, sizeof(evmc_bytes32)));
        bcos::bytes data(l.data.begin(), l.data.end());
        logs.emplace_back(std::move(addr), std::move(topics), std::move(data));
    }
    // All transactions that reach transition() are valid Ethereum transactions.
    // Exceptional halts (stack overflow, invalid opcode, etc.) still produce a
    // valid receipt with status=0 in Ethereum.
    int32_t status = 0;
    bcos::bytes output;
    return rf.createReceipt(
        bcos::u256(static_cast<uint64_t>(er.gas_used)),
        std::string{}, logs, status, bcos::ref(output), blockNumber);
}

struct ZeroBlockHashes : evmone::state::BlockHashes
{
    evmc::bytes32 get_block_hash(int64_t) const noexcept override { return {}; }
};

}  // namespace bcos::executor_v1::eth
