#pragma once

#include <bcos-concepts/Hash.h>
#include <bcos-concepts/Receipt.h>
#include <bcos-concepts/Transaction.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <json/value.h>

namespace bcos::rpc
{
template <bcos::crypto::hasher::Hasher Hasher>
void toJsonResp(bcos::concepts::transaction::Transaction auto const& transaction, Json::Value& resp)
{
    resp["version"] = transaction.version;
    std::string hash;
    bcos::concepts::hash::calculate<Hasher>(transaction, hash);
    resp["hash"] = toHexStringWithPrefix(hash);
    resp["nonce"] = transaction.nonce.str(16);
    resp["blockLimit"] = transaction.blockLimit;
    resp["to"] = transaction.to;
    resp["from"] = toHexStringWithPrefix(transaction.sender);
    resp["input"] = toHexStringWithPrefix(transaction.data.input);
    resp["importTime"] = transaction.importTime;
    resp["chainID"] = std::string(transaction.data.chainID);
    resp["groupID"] = std::string(transaction.data.groupID);
    resp["abi"] = std::string(transaction.data.abi);
    resp["signature"] = toHexStringWithPrefix(transaction.signature);
}

template <bcos::crypto::hasher::Hasher Hasher>
void toJsonResp(bcos::concepts::receipt::TransactionReceipt auto const& receipt,
    std::string_view txHash, Json::Value& jResp)
{
    jResp["version"] = receipt.data.version;
    jResp["contractAddress"] = receipt.data.contractAddress;
    jResp["gasUsed"] = receipt.data.gasUsed;
    jResp["status"] = receipt.data.status;
    jResp["blockNumber"] = receipt.data.blockNumber;
    jResp["output"] = toHexStringWithPrefix(receipt.data.output);
    jResp["message"] = receipt.message;
    jResp["transactionHash"] = std::string(txHash);

    std::string hash;
    bcos::concepts::hash::calculate<Hasher>(receipt, hash);
    jResp["hash"] = toHexStringWithPrefix(hash);
    jResp["logEntries"] = Json::Value(Json::arrayValue);
    for (const auto& logEntry : receipt.data.logEntries)
    {
        Json::Value jLog;
        jLog["address"] = logEntry.address();
        jLog["topics"] = Json::Value(Json::arrayValue);
        for (const auto& topic : logEntry.topics())
        {
            jLog["topics"].append(toHexStringWithPrefix(topic));
        }
        jLog["data"] = toHexStringWithPrefix(logEntry.data);
        jResp["logEntries"].append(std::move(jLog));
    }
}

}  // namespace bcos::rpc