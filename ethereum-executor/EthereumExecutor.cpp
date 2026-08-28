/// @file EthereumExecutor.cpp
/// @brief Out-of-line definitions of the non-template helpers of the
///        pure-Ethereum executor (block-info builder, validation-error receipt).
///        All template logic lives in EthereumExecutor.h + the ported
///        EthereumState.h / EthereumHost.h / EthereumTransition.h headers.

#include "EthereumExecutor.h"

#include "bcos-protocol/TransactionStatus.h"
#include <algorithm>
#include <cstdint>
#include <string>

namespace bcos::executor_v1::eth
{

EthBlockInfo buildBlockInfo(
    protocol::BlockHeader const& header, ledger::LedgerConfig const& config, evmc_revision rev)
{
    EthBlockInfo info{};
    info.number = header.number();
    info.timestamp = header.timestamp() / 1000L;
    info.gas_limit = static_cast<int64_t>(std::get<0>(config.gasLimit()));
    info.difficulty = config.difficulty();
    // EIP-4399 (Paris+): opcode 0x44 is PREVRANDAO, returning the mixHash.
    // Before Paris: opcode 0x44 is DIFFICULTY, returning the block difficulty.
    // The host maps block prev_randao to 0x44 (there is no separate difficulty
    // field in evmc_tx_context), so for pre-Paris forks we must place the
    // DIFFICULTY value into prev_randao for 0x44 to work.
    if (rev >= EVMC_PARIS)
        info.prev_randao = config.prevRandao();
    else
        info.prev_randao = evmc::bytes32{static_cast<uint64_t>(info.difficulty)};
    auto const& cb = header.coinbase();
    if (cb.size() == sizeof(evmc_address))
        std::copy_n(cb.begin(), sizeof(evmc_address), info.coinbase.bytes);
    // base_fee is a hex string that may or may not carry the 0x prefix; parse
    // it the same way as chainId/nonce (bcos::u256), then truncate to uint64.
    auto baseFeeStr = std::get<0>(config.gasPrice());
    if (!baseFeeStr.empty() && baseFeeStr != "0x" && baseFeeStr != "0x0")
    {
        auto baseFeeHex = (baseFeeStr.size() >= 2 && baseFeeStr[0] == '0' && baseFeeStr[1] == 'x') ?
                              baseFeeStr :
                              "0x" + baseFeeStr;
        info.base_fee = static_cast<uint64_t>(bcos::u256(baseFeeHex));
    }
    // EIP-4844 blob gas parameters (Cancun+). The blob base fee is computed from
    // the block's excess blob gas using the per-revision blob schedule (EIP-7840),
    // matching evmone's statetest/blockchaintest loaders:
    //   blob_base_fee = compute_blob_gas_price(blob_params, excess_blob_gas)
    info.excess_blob_gas = config.excessBlobGas();
    info.blob_gas_used = config.blobGasUsed();
    if (rev >= EVMC_CANCUN)
    {
        const auto excess = config.excessBlobGas().value_or(0);
        info.blob_base_fee = evm::compute_blob_gas_price(blobParamsForRevision(rev), excess);
    }
    else
    {
        info.blob_base_fee = std::nullopt;
    }
    return info;
}

protocol::TransactionReceipt::Ptr validationErrorReceipt(std::error_code const& error,
    protocol::TransactionReceiptFactory const& rf, int64_t blockNumber)
{
    // A transaction rejected by validateTransaction never executed, so it
    // consumed no gas and produced no logs. Map the evm::ErrorCode
    // to the closest BCOS TransactionStatus.
    int32_t status = [&]() -> int32_t {
        using protocol::TransactionStatus;
        switch (static_cast<evm::ErrorCode>(error.value()))
        {
        case evm::INTRINSIC_GAS_TOO_LOW:
            return static_cast<int32_t>(TransactionStatus::OutOfGasLimit);
        case evm::INSUFFICIENT_FUNDS:
            return static_cast<int32_t>(TransactionStatus::NotEnoughCash);
        case evm::NONCE_HAS_MAX_VALUE:
        case evm::NONCE_TOO_HIGH:
        case evm::NONCE_TOO_LOW:
            return static_cast<int32_t>(TransactionStatus::NonceCheckFail);
        case evm::SENDER_NOT_EOA:
            return static_cast<int32_t>(TransactionStatus::SenderNoEOA);
        case evm::INIT_CODE_SIZE_LIMIT_EXCEEDED:
            return static_cast<int32_t>(TransactionStatus::MaxInitCodeSizeExceeded);
        case evm::GAS_LIMIT_REACHED:
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
