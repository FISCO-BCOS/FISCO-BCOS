#pragma once

#include <bcos-concepts/Block.h>
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
    std::string_view txHash, Json::Value& resp)
{
    resp["version"] = receipt.data.version;
    resp["contractAddress"] = receipt.data.contractAddress;
    resp["gasUsed"] = receipt.data.gasUsed;
    resp["status"] = receipt.data.status;
    resp["blockNumber"] = receipt.data.blockNumber;
    resp["output"] = toHexStringWithPrefix(receipt.data.output);
    resp["message"] = receipt.message;
    resp["transactionHash"] = std::string(txHash);

    std::string hash;
    bcos::concepts::hash::calculate<Hasher>(receipt, hash);
    resp["hash"] = toHexStringWithPrefix(hash);
    resp["logEntries"] = Json::Value(Json::arrayValue);
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
        resp["logEntries"].append(std::move(jLog));
    }
}

template <bcos::crypto::hasher::Hasher Hasher>
void toJsonResp(
    bcos::concepts::block::Block auto const& block, std::string_view txHash, Json::Value& jResp)
{
    auto const& blockHeader = block.blockHeader;
    std::string hash;
    bcos::concepts::hash::calculate<Hasher>(block, hash);
    jResp["hash"] = toHexStringWithPrefix(hash);
    jResp["version"] = block.version;
    jResp["txsRoot"] = toHexStringWithPrefix(blockHeader.txsRoot);
    jResp["receiptsRoot"] = toHexStringWithPrefix(blockHeader.receiptRoot);
    jResp["stateRoot"] = toHexStringWithPrefix(blockHeader.stateRoot);
    jResp["number"] = blockHeader.blockNumber;
    jResp["gasUsed"] = blockHeader.gasUsed;
    jResp["timestamp"] = blockHeader.timestamp;
    jResp["sealer"] = blockHeader.sealer;
    jResp["extraData"] = toHexStringWithPrefix(blockHeader.extraData);

    jResp["consensusWeights"] = Json::Value(Json::arrayValue);
    for (const auto& wei : blockHeader.consensusWeights)
    {
        jResp["consensusWeights"].append(wei);
    }

    jResp["sealerList"] = Json::Value(Json::arrayValue);
    for (const auto& sealer : blockHeader.sealerList)
    {
        jResp["sealerList"].append(toHexStringWithPrefix(sealer));
    }

    Json::Value jParentInfo(Json::arrayValue);
    for (const auto& parent : blockHeader.parentInfo)
    {
        Json::Value jp;
        jp["blockNumber"] = parent.blockNumber;
        jp["blockHash"] = toHexStringWithPrefix(parent.blockHash);
        jParentInfo.append(std::move(jp));
    }
    jResp["parentInfo"] = std::move(jParentInfo);

    Json::Value jSignList(Json::arrayValue);
    for (const auto& sign : blockHeader.signatureList)
    {
        Json::Value jSign;
        jSign["sealerIndex"] = sign.index;
        jSign["signature"] = toHexStringWithPrefix(sign.signature);
        jSignList.append(std::move(jSign));
    }
    jResp["signatureList"] = std::move(jSignList);
}

}  // namespace bcos::rpc