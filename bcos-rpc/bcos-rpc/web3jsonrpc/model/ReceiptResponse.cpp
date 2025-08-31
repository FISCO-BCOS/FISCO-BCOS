#include "ReceiptResponse.h"
#include "Bloom.h"
#include "bcos-crypto/ChecksumAddress.h"
#include "bcos-rpc/web3jsonrpc/model/Web3Transaction.h"
#include "bcos-utilities/Common.h"
#include "bcos-utilities/DataConvertUtility.h"
#include <cstdint>

void bcos::rpc::combineReceiptResponse(Json::Value& result, protocol::TransactionReceipt& receipt,
    const bcos::protocol::Transaction& tx, const bcos::protocol::Block* block)
{
    if (!result.isObject())
    {
        return;
    }
    uint8_t status = (receipt.status() == 0 ? 1 : 0);
    result["status"] = toQuantity(status);
    result["transactionHash"] = tx.hash().hexPrefixed();
    crypto::HashType blockHash;
    uint64_t blockNumber = 0;
    u256 cumulativeGasUsed = 0;
    size_t logIndex = 0;
    uint64_t transactionIndex = 0;
    if (block != nullptr)
    {
        auto blockHeader = block->blockHeader();
        blockHash = blockHeader->hash();
        blockNumber = blockHeader->number();
        auto hashes = block->transactionHashes();
        auto receipts = block->receipts();
        for (; transactionIndex < block->transactionsHashSize(); transactionIndex++)
        {
            if (transactionIndex < block->receiptsSize())
            {
                auto currentReceipt = receipts[transactionIndex];
                cumulativeGasUsed += currentReceipt->gasUsed();
                logIndex += currentReceipt->logEntries().size();
            }
            if (hashes[transactionIndex] == tx.hash())
            {
                break;
            }
        }
    }
    result["transactionIndex"] = toQuantity(transactionIndex);
    result["blockHash"] = blockHash.hexPrefixed();
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
        log["blockHash"] = blockHash.hexPrefixed();
        log["transactionIndex"] = toQuantity(transactionIndex);
        log["transactionHash"] = tx.hash().hexPrefixed();
        log["removed"] = false;
        result["logs"].append(std::move(log));
        rpc::Log logObj{.address = receiptLog[i].takeAddress(),
            .topics = receiptLog[i].takeTopics(),
            .data = receiptLog[i].takeData()};
        logs.push_back(std::move(logObj));
    }
    auto logsBloom = getLogsBloom(logs);
    result["logsBloom"] = toHexStringWithPrefix(logsBloom);
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
