#include "ReceiptResponse.h"
#include "Log.h"
#include "Web3Transaction.h"
#include "bcos-codec/rlp/OpReceiptMetaCodec.h"
#include "bcos-crypto/ChecksumAddress.h"
#include "bcos-utilities/Common.h"
#include "bcos-utilities/DataConvertUtility.h"
#include <cstdint>

namespace
{
/// Append an OP receipt's extension fields to the JSON response, mirroring op-geth's
/// ethapi.MarshalReceipt (internal/ethapi/api.go:1744-1830). Field presence is driven by the
/// serialized OpReceiptMeta (see OpReceiptMetaCodec.h): a field only appears when the execution
/// layer recorded it, which is what distinguishes Isthmus (operator fees) from Jovian (DA
/// footprint) from plain Ecotone-era L1 passthrough.
void appendOpReceiptMetaFields(Json::Value& result, protocol::TransactionReceipt const& receipt)
{
    auto metaView = receipt.opReceiptMeta();
    if (metaView.empty())
    {
        return;
    }
    bcos::codec::rlp::OpReceiptMetaFields fields;
    if (auto error = bcos::codec::rlp::decodeOpReceiptMeta(
            bcos::bytesConstRef{(bcos::byte const*)metaView.data(), metaView.size()}, fields))
    {
        WEB3_LOG(DEBUG) << "appendOpReceiptMetaFields: invalid opReceiptMeta: "
                        << boost::diagnostic_information(*error);
        return;
    }
    // uint256 fields are hexutil.Big in op-geth — compact big-endian hex (leading zeros trimmed),
    // not fixed-width. toQuantity(BigNumber) goes through toCompactBigEndian for exactly that.
    if (fields.l1_gas_price)
        result["l1GasPrice"] = toQuantity(bcos::fromBigEndian<bcos::u256>(
            bcos::bytesConstRef{fields.l1_gas_price->data(), fields.l1_gas_price->size()}));
    if (fields.l1_fee)
        result["l1Fee"] = toQuantity(bcos::fromBigEndian<bcos::u256>(
            bcos::bytesConstRef{fields.l1_fee->data(), fields.l1_fee->size()}));
    if (fields.l1_blob_base_fee)
        result["l1BlobBaseFee"] = toQuantity(bcos::fromBigEndian<bcos::u256>(
            bcos::bytesConstRef{fields.l1_blob_base_fee->data(), fields.l1_blob_base_fee->size()}));
    if (fields.l1_base_fee_scalar)
        result["l1BaseFeeScalar"] = toQuantity(*fields.l1_base_fee_scalar);
    if (fields.l1_blob_base_fee_scalar)
        result["l1BlobBaseFeeScalar"] = toQuantity(*fields.l1_blob_base_fee_scalar);
    if (fields.operator_fee_scalar)
        result["operatorFeeScalar"] = toQuantity(*fields.operator_fee_scalar);
    if (fields.operator_fee_constant)
        result["operatorFeeConstant"] = toQuantity(*fields.operator_fee_constant);
    if (fields.da_footprint_gas_scalar)
        result["daFootprintGasScalar"] = toQuantity(*fields.da_footprint_gas_scalar);
    if (fields.da_footprint)
        result["blobGasUsed"] = toQuantity(*fields.da_footprint);
    if (fields.l1_gas_used)
        result["l1GasUsed"] = toQuantity(*fields.l1_gas_used);
    if (fields.operator_fee)
        result["operatorFee"] = toQuantity(bcos::fromBigEndian<bcos::u256>(
            bcos::bytesConstRef{fields.operator_fee->data(), fields.operator_fee->size()}));
    if (fields.deposit_nonce)
        result["depositNonce"] = toQuantity(*fields.deposit_nonce);
    if (fields.deposit_receipt_version)
        result["depositReceiptVersion"] = toQuantity(*fields.deposit_receipt_version);
}
}  // namespace

/// Emit the receipt JSON for an OP-block transaction, whose sender/type/input come from a decoded
/// Web3Transaction rather than a bcos::protocol::Transaction (OP blocks carry raw EIP-2718
/// envelopes, not tars Transaction objects — see spec §6.4 f).
void bcos::rpc::combineReceiptResponseFromWeb3(Json::Value& result, const Web3Transaction& tx,
    protocol::TransactionReceipt& receipt, const crypto::HashType& blockHash)
{
    if (!result.isObject())
    {
        return;
    }
    uint8_t status = (receipt.status() == 0 ? 1 : 0);
    result["status"] = toQuantity(status);
    result["transactionHash"] = tx.txHash().hexPrefixed();
    auto cumulativeGasUsed = safeCastToU256(receipt.cumulativeGasUsed());
    size_t logIndex = receipt.logIndex();
    auto transactionIndex = toQuantity(receipt.transactionIndex());
    result["transactionIndex"] = transactionIndex;
    auto blockHashHex = blockHash.hexPrefixed();
    result["blockHash"] = blockHashHex;
    auto blockNumber = receipt.blockNumber();
    result["blockNumber"] = toQuantity(blockNumber);
    auto from = toHex(tx.sender());
    toChecksumAddress(from, bcos::crypto::keccak256Hash(bcos::bytesConstRef(from)).hex());
    result["from"] = "0x" + std::move(from);
    if (!tx.to.has_value())
    {
        result["to"] = Json::nullValue;
    }
    else
    {
        auto to = tx.to.value().hex();
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
    Logs logs;
    logs.reserve(receiptLog.size());
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
        log["transactionHash"] = tx.txHash().hexPrefixed();
        log["removed"] = false;
        result["logs"].append(std::move(log));
        rpc::Log logObj{.address = receiptLog[i].takeAddress(),
            .topics = receiptLog[i].takeTopics(),
            .data = receiptLog[i].takeData()};
        logs.push_back(std::move(logObj));
    }
    result["logsBloom"] = toHexStringWithPrefix(receipt.logsBloom());
    result["type"] = toQuantity(static_cast<uint64_t>(tx.type));
    appendOpReceiptMetaFields(result, receipt);
}

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
    Logs logs;
    logs.reserve(receiptLog.size());
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
        rpc::Log logObj{.address = receiptLog[i].takeAddress(),
            .topics = receiptLog[i].takeTopics(),
            .data = receiptLog[i].takeData()};
        logs.push_back(std::move(logObj));
    }
    result["logsBloom"] = toHexStringWithPrefix(receipt.logsBloom());
    auto type = TransactionType::Legacy;
    if (!tx.extraTransactionBytes().empty())
    {
        if (auto firstByte = tx.extraTransactionBytes()[0];
            firstByte < bcos::codec::rlp::BYTES_HEAD_BASE)
        {
            type = static_cast<TransactionType>(firstByte);
        }
    }
    result["type"] = toQuantity(static_cast<uint64_t>(type));
    // No-op for legacy receipts (opReceiptMeta is empty); OP receipts that reach this path (e.g.
    // the RPC layer resolves an OP tx hash via the generic path first) still get their extension
    // fields.
    appendOpReceiptMetaFields(result, receipt);
}
