/// @file BCOS2Evmone.cpp
/// @brief Out-of-line definitions of the non-template BCOS↔evmone converters
///        declared in BCOS2Evmone.h.
///
/// Only the concrete conversion functions move here; the template helpers
/// (clearAccountStorage / applyStateDiff) and the ZeroBlockHashes type stay
/// in the header because they are used from template code.

#include "BCOS2Evmone.h"

#include "bcos-evm/eth/state/state.hpp"
#include "bcos-evm/eth/state/transaction.hpp"
#include "bcos-framework/protocol/LogEntry.h"
#include "bcos-protocol/TransactionStatus.h"
#include "bcos-utilities/BoostLog.h"
#include "bcos-utilities/DataConvertUtility.h"
#include <algorithm>
#include <cstdint>
#include <string>
#include <system_error>

#define BCOS2EVMONE_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("BCOS2EVMONE")

namespace bcos::executor_v1::eth
{

evmone::state::BlockInfo blockHeaderToBlockInfo(
    protocol::BlockHeader const& header, ledger::LedgerConfig const& config, evmc_revision rev)
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
        info.prev_randao =
            intx::be::store<evmc::bytes32>(intx::uint256(static_cast<uint64_t>(info.difficulty)));
    auto const& cb = header.coinbase();
    if (cb.size() == sizeof(evmc_address))
        std::copy_n(cb.begin(), sizeof(evmc_address), info.coinbase.bytes);
    // base_fee is a hex string that may or may not carry the 0x prefix; parse
    // it the same way as chainId/nonce (bcos::u256), then truncate to uint64
    // as before. The string is always interpreted as hex (the 0x prefix is just
    // added when missing), so an unprefixed value is read exactly as the old
    // std::stoull(s, nullptr, 16) read it. The improvement over stoull is only
    // that bcos::u256 does not throw on a value above uint64 — matching how
    // chainId/nonce are parsed elsewhere — and the truncation below is the only
    // place a value too large for base_fee would be noticed.
    auto baseFeeStr = std::get<0>(config.gasPrice());
    if (!baseFeeStr.empty() && baseFeeStr != "0x" && baseFeeStr != "0x0")
    {
        auto baseFeeHex = (baseFeeStr.size() >= 2 && baseFeeStr[0] == '0' && baseFeeStr[1] == 'x') ?
                              baseFeeStr :
                              "0x" + baseFeeStr;
        info.base_fee = static_cast<uint64_t>(bcos::u256(baseFeeHex));
    }
    // EIP-4844 blob gas parameters (Cancun+). The blob base fee is computed from
    // the block's excess blob gas using the per-revision blob schedule (EIP-7840).
    // Matches evmone's statetest/blockchaintest loaders:
    //   blob_base_fee = compute_blob_gas_price(blob_params, excess_blob_gas)
    // The per-revision schedule constants are shared with EthereumExecutor via
    // blobParamsForRevision() in BCOS2Evmone.h.
    info.excess_blob_gas = config.excessBlobGas();
    info.blob_gas_used = config.blobGasUsed();
    if (rev >= EVMC_CANCUN)
    {
        const auto excess = config.excessBlobGas().value_or(0);
        info.blob_base_fee =
            evmone::state::compute_blob_gas_price(blobParamsForRevision(rev), excess);
    }
    else
    {
        info.blob_base_fee = std::nullopt;
    }
    return info;
}

evmone::state::Transaction bcosTransactionToEvmone(protocol::Transaction const& tx)
{
    evmone::state::Transaction evmTx{};
    switch (tx.web3TypedTxKind())
    {
    case 0:
        evmTx.type = evmone::state::Transaction::Type::legacy;
        break;
    case 1:
        evmTx.type = evmone::state::Transaction::Type::access_list;
        break;
    case 2:
        evmTx.type = evmone::state::Transaction::Type::eip1559;
        break;
    case 3:
        evmTx.type = evmone::state::Transaction::Type::blob;
        break;
    case 4:
        evmTx.type = evmone::state::Transaction::Type::set_code;
        break;
    default:
        break;
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
    if (!tb.empty())
    {
        // `to` is uniformly an ASCII hex address string ("0x..." / 40 hex chars)
        // on every tx path: Web3Transaction::takeToTarsTransaction() writes
        // hexPrefixed(), RPC/txpool treat it as hex (TxValidator::isValidToField
        // requires exactly 40 hex chars), and the test helpers use the same
        // "0x..." encoding. The raw 20-byte branch below is kept only as a
        // defensive fallback.
        const bool has0x = tb.size() >= 2 && tb[0] == '0' && (tb[1] == 'x' || tb[1] == 'X');
        const bool is40Hex =
            tb.size() == sizeof(evmc_address) * 2 && std::all_of(tb.begin(), tb.end(), [](char c) {
                return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
            });
        if (has0x || is40Hex)
        {
            // Hex-string form. Only a well-formed 20-byte address decodes to a
            // valid recipient: a short decode (e.g. "0x1234" -> 2 bytes) or a
            // malformed one (e.g. "0x" or "0xzz") must NOT be left-aligned into
            // a 20-byte address — Ethereum addresses are big-endian and
            // right-aligned, and copying a short value to the front would
            // produce a wrong (or all-zero) address. Such input leaves `to`
            // unset, i.e. contract creation, matching base behaviour for
            // anything that is not a well-formed address.
            if (auto decoded = safeFromHex(tb); decoded && decoded->size() == sizeof(evmc_address))
            {
                evmc_address ta{};
                std::copy(decoded->begin(), decoded->end(), ta.bytes);
                evmTx.to = ta;
            }
        }
        else if (tb.size() == sizeof(evmc_address))
        {
            // Defensive fallback for raw 20-byte addresses. No production tx path uses this
            // encoding today (EEMakeTransferTx and every RPC/txpool path write hex), but if it
            // ever appears it must be a full 20-byte value — a shorter copy would silently
            // fabricate an address out of the first bytes. Log a warning so an unexpected
            // input encoding that reaches this branch surfaces instead of passing silently.
            BCOS2EVMONE_LOG(WARNING)
                << "to field uses raw 20-byte encoding (fallback branch); not expected "
                   "from any production tx path";
            evmc_address ta{};
            std::copy_n(tb.begin(), sizeof(evmc_address), ta.bytes);
            evmTx.to = ta;
        }
        // Anything else (short raw bytes, malformed hex) leaves `to` unset —
        // contract creation, never a transfer to address(0).
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
    // RPC/Web3 decoding stores values with 0x prefix (toQuantity). Parse strictly and
    // non-throwing via the shared helper (fromQuantity / safeFromQuantity), so a
    // double 0x (e.g. "0x0x1a"), a non-numeric value (e.g. a FISCO chain_id like
    // "chain0"), a sign, trailing garbage, or a value above uint64 all fall through to 0.
    // A value that cannot be parsed is left at 0. Note that 0 is NOT a
    // guaranteed rejection:
    //   * chain_id 0 never matches a real chain id (and evmone's
    //     validate_transaction does not check chain_id — the signature binds it
    //     upstream, so a malformed chain_id implies an invalid signature that
    //     admission rejects before this executor sees it);
    //   * nonce 0 IS a valid nonce for a fresh account, so a malformed nonce
    //     can only be accepted when the sender's nonce is 0. Like chain_id, the
    //     nonce is part of the signed payload, so a malformed nonce means the
    //     signature check failed upstream — this fallback only ever matters for
    //     byzantine/unsigned inputs.
    evmTx.chain_id = bcos::safeFromQuantity(tx.chainId()).value_or(0);
    evmTx.nonce = bcos::safeFromQuantity(tx.nonce()).value_or(0);
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

bcos::u256 toBcosU256(intx::uint256 const& val)
{
    return bcos::u256(intx::to_string(val));
}

protocol::TransactionReceipt::Ptr evmoneReceiptToBcos(evmone::state::TransactionReceipt const& er,
    protocol::TransactionReceiptFactory const& rf, int64_t blockNumber)
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
        case EVMC_SUCCESS:
            return static_cast<int32_t>(protocol::TransactionStatus::None);
        case EVMC_REVERT:
            return static_cast<int32_t>(protocol::TransactionStatus::RevertInstruction);
        case EVMC_OUT_OF_GAS:
            return static_cast<int32_t>(protocol::TransactionStatus::OutOfGas);
        case EVMC_UNDEFINED_INSTRUCTION:
        case EVMC_INVALID_INSTRUCTION:
            return static_cast<int32_t>(protocol::TransactionStatus::BadInstruction);
        case EVMC_BAD_JUMP_DESTINATION:
            return static_cast<int32_t>(protocol::TransactionStatus::BadJumpDestination);
        case EVMC_STACK_OVERFLOW:
            return static_cast<int32_t>(protocol::TransactionStatus::OutOfStack);
        case EVMC_STACK_UNDERFLOW:
            return static_cast<int32_t>(protocol::TransactionStatus::StackUnderflow);
        case EVMC_INSUFFICIENT_BALANCE:
            return static_cast<int32_t>(protocol::TransactionStatus::NotEnoughCash);
        case EVMC_INVALID_MEMORY_ACCESS:
        case EVMC_CALL_DEPTH_EXCEEDED:
        case EVMC_STATIC_MODE_VIOLATION:
        case EVMC_PRECOMPILE_FAILURE:
        case EVMC_CONTRACT_VALIDATION_FAILURE:
        case EVMC_INTERNAL_ERROR:
        case EVMC_REJECTED:
        case EVMC_OUT_OF_MEMORY:
        default:
            return static_cast<int32_t>(protocol::TransactionStatus::Unknown);
        }
    }();
    // evmone state::TransactionReceipt does not carry output data;
    // return data is consumed during execution and not stored per spec.
    bcos::bytes output;
    return rf.createReceipt(bcos::u256(static_cast<uint64_t>(er.gas_used)), std::string{}, logs,
        status, bcos::ref(output), blockNumber);
}

protocol::TransactionReceipt::Ptr validationErrorReceipt(std::error_code const& error,
    protocol::TransactionReceiptFactory const& rf, int64_t blockNumber)
{
    // A transaction rejected by evmone's validate_transaction never executed,
    // so it consumed no gas and produced no logs. Map the evmone::state::ErrorCode
    // to the closest BCOS TransactionStatus.
    int32_t status = [&]() -> int32_t {
        using protocol::TransactionStatus;
        switch (static_cast<evmone::state::ErrorCode>(error.value()))
        {
        case evmone::state::INTRINSIC_GAS_TOO_LOW:
            return static_cast<int32_t>(TransactionStatus::OutOfGasLimit);
        case evmone::state::INSUFFICIENT_FUNDS:
            return static_cast<int32_t>(TransactionStatus::NotEnoughCash);
        case evmone::state::NONCE_HAS_MAX_VALUE:
        case evmone::state::NONCE_TOO_HIGH:
        case evmone::state::NONCE_TOO_LOW:
            return static_cast<int32_t>(TransactionStatus::NonceCheckFail);
        case evmone::state::SENDER_NOT_EOA:
            return static_cast<int32_t>(TransactionStatus::SenderNoEOA);
        case evmone::state::INIT_CODE_SIZE_LIMIT_EXCEEDED:
            return static_cast<int32_t>(TransactionStatus::MaxInitCodeSizeExceeded);
        case evmone::state::GAS_LIMIT_REACHED:
            return static_cast<int32_t>(TransactionStatus::BlockLimitCheckFail);
        default:
            return static_cast<int32_t>(TransactionStatus::Unknown);
        }
    }();
    bcos::bytes output;
    return rf.createReceipt(
        bcos::u256(0), std::string{}, {}, status, bcos::ref(output), blockNumber);
}

}  // namespace bcos::executor_v1::eth
