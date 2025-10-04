#include "ReceiptResponse.h"
#include "Log.h"
#include "Web3Transaction.h"
#include "bcos-crypto/ChecksumAddress.h"
#include "bcos-utilities/Common.h"
#include "bcos-utilities/DataConvertUtility.h"
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
    u256 cumulativeGasUsed = boost::lexical_cast<u256>(receipt.cumulativeGasUsed());
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
}
