#pragma once

#include <bcos-concepts/Block.h>
#include <bcos-concepts/ByteBuffer.h>
#include <bcos-concepts/Hash.h>
#include <bcos-concepts/Receipt.h>
#include <bcos-concepts/Transaction.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <bits/ranges_util.h>
#include <json/value.h>
#include <boost/algorithm/hex.hpp>
#include <iterator>

namespace bcos::rpc
{

void hex2Bin(bcos::concepts::bytebuffer::ByteBuffer auto const& hex,
    bcos::concepts::bytebuffer::ByteBuffer auto& out)
{
    auto view =
        RANGES::subrange<decltype(RANGES::begin(hex))>(RANGES::begin(hex), RANGES::end(hex));

    if (RANGES::size(view) >= 2 && (view[0] == '0' && view[1] == 'x'))
    {
        view = RANGES::subrange<decltype(RANGES::begin(hex))>(
            RANGES::begin(hex) + 2, RANGES::end(hex));
    }

    if ((RANGES::size(view) % 2 != 0)) [[unlikely]]
        BOOST_THROW_EXCEPTION(std::invalid_argument{"Invalid input hex string!"});

    bcos::concepts::resizeTo(out, RANGES::size(view) / 2);
    boost::algorithm::unhex(RANGES::begin(view), RANGES::end(view),
        (RANGES::range_value_t<decltype(view)>*)RANGES::data(out));
}

template <bcos::crypto::hasher::Hasher Hasher>
void toJsonResp(bcos::concepts::transaction::Transaction auto const& transaction, Json::Value& resp)
{
    resp["version"] = transaction.data.version;
    std::string hash;
    bcos::concepts::hash::calculate<Hasher>(transaction, hash);
    resp["hash"] = toHexStringWithPrefix(hash);
    resp["nonce"] = transaction.data.nonce;
    resp["blockLimit"] = Json::Value((Json::Int64)transaction.data.blockLimit);
    resp["to"] = transaction.data.to;
    resp["from"] = toHexStringWithPrefix(transaction.sender);
    resp["input"] = toHexStringWithPrefix(transaction.data.input);
    resp["importTime"] = (Json::Int64)transaction.importTime;
    resp["chainID"] = std::string(transaction.data.chainID);
    resp["groupID"] = std::string(transaction.data.groupID);
    resp["abi"] = std::string(transaction.data.abi);
    resp["signature"] = toHexStringWithPrefix(transaction.signature);
}

template <bcos::crypto::hasher::Hasher Hasher>
void toJsonResp(bcos::concepts::receipt::TransactionReceipt auto const& receipt,
    std::string_view txHash, Json::Value& resp)
{
    resp["version"] = Json::Value((Json::Int64)receipt.data.version);
    resp["contractAddress"] = receipt.data.contractAddress;
    resp["gasUsed"] = receipt.data.gasUsed;
    resp["status"] = Json::Value((Json::Int64)receipt.data.status);
    resp["blockNumber"] = Json::Value((Json::Int64)receipt.data.blockNumber);
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

        auto topisc = Json::Value(Json::arrayValue);
        for (const auto& topic : logEntry.topic)
        {
            topisc.append(toHexStringWithPrefix(topic));
        }
        jLog["topics"] = std::move(topisc);
        jLog["data"] = toHexStringWithPrefix(logEntry.data);
        resp["logEntries"].append(std::move(jLog));
    }
}

template <bcos::crypto::hasher::Hasher Hasher>
void toJsonResp(bcos::concepts::block::Block auto const& block, Json::Value& jResp, bool onlyHeader)
{
    auto const& blockHeader = block.blockHeader;
    std::string hash;
    bcos::concepts::hash::calculate<Hasher>(block, hash);
    jResp["hash"] = toHexStringWithPrefix(hash);
    jResp["version"] = Json::Value((Json::Int64)block.version);
    jResp["txsRoot"] = toHexStringWithPrefix(blockHeader.data.txsRoot);
    jResp["receiptsRoot"] = toHexStringWithPrefix(blockHeader.data.receiptRoot);
    jResp["stateRoot"] = toHexStringWithPrefix(blockHeader.data.stateRoot);
    jResp["number"] = Json::Value((Json::Int64)blockHeader.data.blockNumber);
    jResp["gasUsed"] = blockHeader.data.gasUsed;
    jResp["timestamp"] = Json::Value((Json::Int64)blockHeader.data.timestamp);
    jResp["sealer"] = Json::Value((Json::Int64)blockHeader.data.sealer);
    jResp["extraData"] = toHexStringWithPrefix(blockHeader.data.extraData);

    jResp["consensusWeights"] = Json::Value(Json::arrayValue);
    for (const auto& weight : blockHeader.data.consensusWeights)
    {
        jResp["consensusWeights"].append(Json::Value((Json::Int64)weight));
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
        jp["blockNumber"] = Json::Value((Json::Int64)parent.blockNumber);
        jp["blockHash"] = toHexStringWithPrefix(parent.blockHash);
        jParentInfo.append(std::move(jp));
    }
    jResp["parentInfo"] = std::move(jParentInfo);

    Json::Value jSignList(Json::arrayValue);
    for (const auto& sign : blockHeader.signatureList)
    {
        Json::Value jSign;
        jSign["sealerIndex"] = Json::Value((Json::Int64)sign.sealerIndex);
        jSign["signature"] = toHexStringWithPrefix(sign.signature);
        jSignList.append(std::move(jSign));
    }
    jResp["signatureList"] = std::move(jSignList);

    if (!onlyHeader)
    {
        Json::Value transactions(Json::arrayValue);
        for (auto const& transaction : block.transactions)
        {
            Json::Value transactionObject;
            toJsonResp<Hasher>(transaction, transactionObject);

            transactions.append(std::move(transactionObject));
        }
        jResp["transactions"] = std::move(transactions);
    }
}

}  // namespace bcos::rpc