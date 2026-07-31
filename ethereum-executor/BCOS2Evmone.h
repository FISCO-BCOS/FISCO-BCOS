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
    protocol::BlockHeader const& header, ledger::LedgerConfig const& config,
    evmc_revision rev)
{
    evmone::state::BlockInfo info{};
    info.number = header.number();
    info.timestamp = header.timestamp() / 1000L;
    info.gas_limit = static_cast<int64_t>(std::get<0>(config.gasLimit()));
    info.difficulty = config.difficulty();
    // EIP-4399 (Paris+): opcode 0x44 is PREVRANDAO, returning the mixHash.
    // Before Paris: opcode 0x44 is DIFFICULTY, returning the block difficulty.
    // evmone's Host::get_tx_context() maps block_prev_randao <- m_block.prev_randao
    // (there is no separate difficulty field in evmc_tx_context), so for pre-Paris
    // forks we must place the DIFFICULTY value into prev_randao for 0x44 to work.
    if (rev >= EVMC_PARIS)
        info.prev_randao = config.prevRandao();
    else
        info.prev_randao = intx::be::store<evmc::bytes32>(
            intx::uint256(static_cast<uint64_t>(info.difficulty)));
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
    // EIP-4844 blob gas parameters (Cancun+). The blob base fee is computed from
    // the block's excess blob gas using the per-revision blob schedule (EIP-7840).
    // Matches evmone's statetest/blockchaintest loaders:
    //   blob_base_fee = compute_blob_gas_price(blob_params, excess_blob_gas)
    info.excess_blob_gas = config.excessBlobGas();
    info.blob_gas_used = config.blobGasUsed();
    if (rev >= EVMC_CANCUN)
    {
        evmone::state::BlobParams blobParams{};
        if (rev >= EVMC_PRAGUE)
        {
            // EIP-7840 blob schedule: Prague/Osaka (target=6, max=9).
            blobParams = {6, 9, 5007716};
        }
        else  // EVMC_CANCUN
        {
            // EIP-7840 blob schedule: Cancun (target=3, max=6).
            blobParams = {3, 6, 3338477};
        }
        const auto excess = config.excessBlobGas().value_or(0);
        info.blob_base_fee = evmone::state::compute_blob_gas_price(blobParams, excess);
    }
    else
    {
        info.blob_base_fee = std::nullopt;
    }
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
    // chainId and nonce from BCOS tx may or may not have 0x prefix.
    // RPC/Web3 decoding stores values with 0x prefix (toQuantity).
    // Guard against double 0x (e.g. "0x0x1a") which would throw.
    auto cid = tx.chainId();
    if (!cid.empty())
    {
        auto cidStr = std::string(cid);
        if (cidStr.size() >= 2 && cidStr[0] == '0' && cidStr[1] == 'x')
            evmTx.chain_id = static_cast<uint64_t>(bcos::u256(cidStr));
        else
            evmTx.chain_id = static_cast<uint64_t>(bcos::u256("0x" + cidStr));
    }
    auto nonceStr = std::string(tx.nonce());
    if (!nonceStr.empty())
    {
        if (nonceStr.size() >= 2 && nonceStr[0] == '0' && nonceStr[1] == 'x')
            evmTx.nonce = static_cast<uint64_t>(bcos::u256(nonceStr));
        else
            evmTx.nonce = static_cast<uint64_t>(bcos::u256("0x" + nonceStr));
    }
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

/// Remove all storage slots of an account (keeps only the fixed account fields).
/// Used when an account self-destructs: its full state (including storage) must
/// be cleared so that a later CREATE/CREATE2 at the same address is not treated
/// as an EIP-7610 collision.
template <class Storage>
task::Task<void> clearAccountStorage(Storage& storage,
    bcos::ledger::account::EVMAccount<Storage>& acc)
{
    using namespace bcos::ledger;
    using namespace bcos::ledger::account;
    auto tableName = co_await acc.path();

    std::vector<executor_v1::StateKey> keysToRemove;
    auto it = co_await storage2::range(storage, storage2::RANGE_SEEK,
        executor_v1::StateKey{tableName, std::string_view{}});
    while (auto kv = co_await it.next())
    {
        auto const& [k, v] = *kv;
        executor_v1::StateKeyView view(k);
        if (view.m_table != tableName)
            break;  // Left this account's table.
        auto key = view.m_key;
        if (key != ACCOUNT_TABLE_FIELDS::NONCE && key != ACCOUNT_TABLE_FIELDS::BALANCE &&
            key != ACCOUNT_TABLE_FIELDS::CODE_HASH && key != ACCOUNT_TABLE_FIELDS::CODE &&
            key != ACCOUNT_TABLE_FIELDS::ABI && key != ACCOUNT_TABLE_FIELDS::ALIVE &&
            key != ACCOUNT_TABLE_FIELDS::FROZEN && key != ACCOUNT_TABLE_FIELDS::SHARD)
        {
            keysToRemove.emplace_back(k);
        }
    }
    if (!keysToRemove.empty())
        co_await storage2::removeSome(storage, keysToRemove);
}

template <class Storage>
task::Task<void> applyStateDiff(Storage& storage, evmone::state::StateDiff const& diff,
    evmc_revision, crypto::Hash const& hashImpl)
{
    using namespace bcos::ledger::account;

    // Phase 1: Process modified_accounts FIRST.
    // This ensures created accounts exist before we potentially delete them.
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

    // Phase 2: Process deleted_accounts, but skip addresses that were just
    // created/modified in Phase 1 (recreated accounts).
    // Also track which addresses we processed as deleted.
    for (auto const& addr : diff.deleted_accounts)
    {
        // Check if this address was already handled by modified_accounts above.
        // If so, the account was destructed and recreated in the same tx —
        // the modified state already reflects the recreation, so skip deletion.
        bool inModified = false;
        for (auto const& m : diff.modified_accounts)
        {
            if (memcmp(m.addr.bytes, addr.bytes, sizeof(evmc_address)) == 0)
            {
                inModified = true;
                break;
            }
        }
        if (inModified)
            continue;  // Already handled by modified_accounts processing

        // Genuine deletion: clear account state (including storage, so that a
        // later CREATE/CREATE2 at this address is not an EIP-7610 collision).
        EVMAccount<Storage> acc(storage, addr, false);
        if (co_await acc.exists())
        {
            co_await acc.setBalance(0);
            co_await acc.setNonce("0");
            co_await acc.setCode(bcos::bytes{}, std::string{}, bcos::h256{});
            co_await clearAccountStorage(storage, acc);
        }
    }

    // NOTE: evmone's State::build_diff() may not include recreated accounts
    // (destructed + recreated via CREATE2 in the same tx) in modified_accounts.
    // This is a known limitation — without modifying bcos-evm, we cannot
    // recover the lost nonce/balance/code for these accounts.
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
    // Map evmc_status_code to FISCO internal status convention:
    // 0 = success (TransactionStatus::None), non-zero = failure.
    // The 0↔1 flip for Ethereum JSON-RPC is done by ReceiptResponse.cpp.
    int32_t status = [&]() -> int32_t {
        switch (er.status)
        {
        case EVMC_SUCCESS: return 0;
        case EVMC_REVERT: return 16;   // TransactionStatus::RevertInstruction
        case EVMC_OUT_OF_GAS: return 12;   // TransactionStatus::OutOfGas
        case EVMC_UNDEFINED_INSTRUCTION:
        case EVMC_INVALID_INSTRUCTION:
            return 11;  // TransactionStatus::BadJumpDestination
        default: return 2;  // non-zero failure
        }
    }();
    // evmone state::TransactionReceipt does not carry output data;
    // return data is consumed during execution and not stored per spec.
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
