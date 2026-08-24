#include "ReceiptResponse.h"
#include "Log.h"
#include "Web3Transaction.h"
#include "bcos-crypto/ChecksumAddress.h"
#include "bcos-utilities/Common.h"
#include "bcos-utilities/DataConvertUtility.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <cstdint>

void bcos::rpc::combineReceiptResponse(Json::Value& result, protocol::TransactionReceipt& receipt,
    const bcos::protocol::Transaction& tx, const crypto::HashType& blockHash)
{
    if (!result.isObject())
    {
        return;
    }
    uint8_t status = (receipt.status() == 0 ? 1 : 0);
    result["status"] = toQuantity(status);
    auto txHashHex = tx.hash().hexPrefixed();
    result["transactionHash"] = txHashHex;
    auto cumulativeGasUsed = safeCastToU256(receipt.cumulativeGasUsed());
    size_t logIndex = receipt.logIndex();
    auto transactionIndex = toQuantity(receipt.transactionIndex());
    result["transactionIndex"] = transactionIndex;
    auto blockHashHex = blockHash.hexPrefixed();
    result["blockHash"] = blockHashHex;
    auto blockNumber = receipt.blockNumber();
    result["blockNumber"] = toQuantity(blockNumber);
    auto from = toHex(tx.sender());
    // EIP-55 checksum needs keccak256(address) per recipient; RPC read path (not consensus),
    // so the 3-4 hashes per receipt are acceptable — caching here would need shared-state
    // synchronization for a marginal win (see review Finding J).
    toChecksumAddress(from, bcos::crypto::keccak256Hash(bcos::bytesConstRef(from)).hex());
    result["from"] = "0x" + std::move(from);
    if (tx.to().empty())
    {
        result["to"] = Json::nullValue;
    }
    else
    {
        auto toView = tx.to();
        auto to = std::string(toView.starts_with("0x") ? toView.substr(2) : toView);
        toChecksumAddress(to, bcos::crypto::keccak256Hash(bcos::bytesConstRef(to)).hex());
        result["to"] = "0x" + std::move(to);
    }
    result["cumulativeGasUsed"] = toQuantity(cumulativeGasUsed);
    result["effectiveGasPrice"] =
        receipt.effectiveGasPrice().empty() ? "0x0" : std::string(receipt.effectiveGasPrice());
    result["gasUsed"] = toQuantity(receipt.gasUsed());
    if (receipt.contractAddress().empty())
    {
        result["contractAddress"] = Json::nullValue;
    }
    else
    {
        auto contractAddress = std::string(receipt.contractAddress());
        toChecksumAddress(contractAddress,
            bcos::crypto::keccak256Hash(bcos::bytesConstRef(contractAddress)).hex());
        result["contractAddress"] = "0x" + std::move(contractAddress);
    }
    result["logs"] = Json::arrayValue;
    auto* mutableReceipt = std::addressof(receipt);
    auto receiptLog = mutableReceipt->takeLogEntries();
    for (size_t i = 0; i < receiptLog.size(); i++)
    {
        Json::Value log;
        auto address = std::string(receiptLog[i].address());
        toChecksumAddress(address, bcos::crypto::keccak256Hash(bcos::bytesConstRef(address)).hex());
        log["address"] = "0x" + std::move(address);
        log["topics"] = Json::arrayValue;
        for (const auto& topic : receiptLog[i].topics())
        {
            log["topics"].append(topic.hexPrefixed());
        }
        log["data"] = toHexStringWithPrefix(receiptLog[i].data());
        log["logIndex"] = toQuantity(logIndex + i);
        log["blockNumber"] = toQuantity(blockNumber);
        log["blockHash"] = blockHashHex;
        log["transactionIndex"] = toQuantity(transactionIndex);
        log["transactionHash"] = txHashHex;
        log["removed"] = false;
        result["logs"].append(std::move(log));
    }
    result["logsBloom"] = toHexStringWithPrefix(receipt.logsBloom());
    // EIP-2718 tx type: for a Web3 tx the authoritative kind is the `web3TypedTxKind` tars slot,
    // populated by Web3Transaction::takeToTarsTransaction for EVERY Web3 kind (Legacy=0,
    // EIP-2930=1, EIP-1559=2, EIP-4844=3, Deposit=0x7e). Byte-sniffing extraTransactionBytes was
    // wrong for typed non-deposit txs: the write side stores encodeForSign() there (RLP WITHOUT
    // the type byte, first byte is a 0xC0+ list header), so the old `< BYTES_HEAD_BASE` sniff
    // collapsed EIP-2930/1559/4844 receipts into Legacy 0x0 — while eth_getTransactionByHash
    // correctly reported 0x01/0x02/0x03. For non-Web3 (BCOS) txs web3TypedTxKind() is 0 == Legacy,
    // matching the old empty-extraTransactionBytes path.
    auto type = TransactionType::Legacy;
    if (tx.type() == bcos::protocol::TransactionType::Web3Transaction)
    {
        // web3TypedTxKind is a display-grade mirror byte — value-domain check rather than a bare
        // static_cast so a forged/unknown kind renders as Legacy instead of a garbage number.
        type = magic_enum::enum_cast<TransactionType>(tx.web3TypedTxKind()).value_or(type);
    }
    result["type"] = toQuantity(static_cast<uint64_t>(type));
    // OP extension fields (aligned with op-geth MarshalReceipt). Empty opStackMeta → no output
    // (never zero/default values); each field is null-checked independently and never throws (D7).
    if (auto meta = receipt.opStackMeta())
    {
        if (meta->l1_gas_price)
            result["l1GasPrice"] = toQuantity(*meta->l1_gas_price);
        if (meta->l1_gas_used)
            result["l1GasUsed"] = toQuantity(*meta->l1_gas_used);
        if (meta->l1_fee)
            result["l1Fee"] = toQuantity(*meta->l1_fee);
        if (meta->l1_blob_base_fee)
            result["l1BlobBaseFee"] = toQuantity(*meta->l1_blob_base_fee);
        if (meta->l1_base_fee_scalar)
            result["l1BaseFeeScalar"] = toQuantity(*meta->l1_base_fee_scalar);
        if (meta->l1_blob_base_fee_scalar)
            result["l1BlobBaseFeeScalar"] = toQuantity(*meta->l1_blob_base_fee_scalar);
        if (meta->operator_fee_scalar)
            result["operatorFeeScalar"] = toQuantity(*meta->operator_fee_scalar);
        if (meta->operator_fee_constant)
            result["operatorFeeConstant"] = toQuantity(*meta->operator_fee_constant);
        if (meta->da_footprint_gas_scalar)
            result["daFootprintGasScalar"] = toQuantity(*meta->da_footprint_gas_scalar);
        if (meta->da_footprint)
            result["blobGasUsed"] = toQuantity(*meta->da_footprint);  // Jovian reuses da_footprint
        if (meta->deposit_nonce)
            result["depositNonce"] = toQuantity(*meta->deposit_nonce);
        if (meta->deposit_receipt_version)
            result["depositReceiptVersion"] = toQuantity(*meta->deposit_receipt_version);
        if (meta->operator_fee)
            result["operatorFee"] = toQuantity(*meta->operator_fee);  // FISCO extension
    }
}
