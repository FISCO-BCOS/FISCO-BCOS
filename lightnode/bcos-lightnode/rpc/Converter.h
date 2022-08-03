#pragma once

#include <bcos-concepts/Block.h>
#include <bcos-concepts/Hash.h>
#include <bcos-concepts/Receipt.h>
#include <bcos-concepts/Transaction.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <json/value.h>
#include <boost/algorithm/hex.hpp>
#include <iterator>

namespace bcos::rpc
{
template <bcos::crypto::hasher::Hasher Hasher>
void toJsonResp(bcos::concepts::transaction::Transaction auto const& transaction, Json::Value& resp)
{
    resp["version"] = transaction.data.version;
    std::string hash;
    bcos::concepts::hash::calculate<Hasher>(transaction, hash);
    resp["hash"] = toHexStringWithPrefix(hash);

    std::string nonce;
    boost::algorithm::hex_lower(RANGES::begin(transaction.data.nonce),
        RANGES::end(transaction.data.nonce), std::back_inserter(nonce));
    resp["nonce"] = std::move(nonce);
    resp["blockLimit"] = Json::Value(transaction.data.blockLimit);
    resp["to"] = transaction.data.to;
    resp["from"] = toHexStringWithPrefix(transaction.sender);
    resp["input"] = toHexStringWithPrefix(transaction.data.input);
    resp["importTime"] = Json::Value(transaction.importTime);
    resp["chainID"] = std::string(transaction.data.chainID);
    resp["groupID"] = std::string(transaction.data.groupID);
    resp["abi"] = std::string(transaction.data.abi);
    resp["signature"] = toHexStringWithPrefix(transaction.signature);
}

template <bcos::crypto::hasher::Hasher Hasher>
void toJsonResp(bcos::concepts::receipt::TransactionReceipt auto const& receipt,
    std::string_view txHash, Json::Value& resp)
{
    resp["version"] = Json::Value(receipt.data.version);
    resp["contractAddress"] = receipt.data.contractAddress;
    resp["gasUsed"] = receipt.data.gasUsed;
    resp["status"] = Json::Value(receipt.data.status);
    resp["blockNumber"] = Json::Value(receipt.data.blockNumber);
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
        jLog["address"] = logEntry.address;
        jLog["topics"] = Json::Value(Json::arrayValue);
        for (const auto& topic : logEntry.topic)
        {
            jLog["topics"].append(toHexStringWithPrefix(topic));
        }
        jLog["data"] = toHexStringWithPrefix(logEntry.data);
        resp["logEntries"].append(std::move(jLog));
    }
}

template <bcos::crypto::hasher::Hasher Hasher>
void toJsonResp(bcos::concepts::block::Block auto const& block, Json::Value& jResp)
{
    auto const& blockHeader = block.blockHeader;
    std::string hash;
    bcos::concepts::hash::calculate<Hasher>(block, hash);
    jResp["hash"] = toHexStringWithPrefix(hash);
    jResp["version"] = Json::Value(block.version);
    jResp["txsRoot"] = toHexStringWithPrefix(blockHeader.data.txsRoot);
    jResp["receiptsRoot"] = toHexStringWithPrefix(blockHeader.data.receiptRoot);
    jResp["stateRoot"] = toHexStringWithPrefix(blockHeader.data.stateRoot);
    jResp["number"] = Json::Value(blockHeader.data.blockNumber);
    jResp["gasUsed"] = blockHeader.data.gasUsed;
    jResp["timestamp"] = Json::Value(blockHeader.data.timestamp);
    jResp["sealer"] = Json::Value(blockHeader.data.sealer);
    jResp["extraData"] = toHexStringWithPrefix(blockHeader.data.extraData);

    jResp["consensusWeights"] = Json::Value(Json::arrayValue);
    for (const auto& weight : blockHeader.data.consensusWeights)
    {
        jResp["consensusWeights"].append(Json::Value(weight));
    }

    jResp["sealerList"] = Json::Value(Json::arrayValue);
    for (const auto& sealer : blockHeader.data.sealerList)
    {
        jResp["sealerList"].append(toHexStringWithPrefix(sealer));
    }

    Json::Value jParentInfo(Json::arrayValue);
    for (const auto& parent : blockHeader.data.parentInfo)
    {
        Json::Value jp;
        jp["blockNumber"] = Json::Value(parent.blockNumber);
        jp["blockHash"] = toHexStringWithPrefix(parent.blockHash);
        jParentInfo.append(std::move(jp));
    }
    jResp["parentInfo"] = std::move(jParentInfo);

    Json::Value jSignList(Json::arrayValue);
    for (const auto& sign : blockHeader.signatureList)
    {
        Json::Value jSign;
        jSign["sealerIndex"] = Json::Value(sign.sealerIndex);
        jSign["signature"] = toHexStringWithPrefix(sign.signature);
        jSignList.append(std::move(jSign));
    }
    jResp["signatureList"] = std::move(jSignList);
}

}  // namespace bcos::rpc