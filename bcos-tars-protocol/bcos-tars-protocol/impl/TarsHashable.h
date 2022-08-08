#pragma once
#include "../Common.h"
#include "bcos-tars-protocol/tars/TransactionReceipt.h"
#include <bcos-concepts/Basic.h>
#include <bcos-concepts/ByteBuffer.h>
#include <bcos-crypto/hasher/Hasher.h>
#include <bcos-tars-protocol/tars/Block.h>
#include <bcos-tars-protocol/tars/Transaction.h>
#include <boost/endian/conversion.hpp>
#include <string>
#include <vector>

namespace bcos::concepts::hash
{

template <bcos::crypto::hasher::Hasher Hasher>
void impl_calculate(
    bcostars::Transaction const& transaction, bcos::concepts::bytebuffer::ByteBuffer auto& out)
{
    if (!transaction.dataHash.empty())
    {
        bcos::concepts::bytebuffer::assignTo(transaction.dataHash, out);
        return;
    }

    Hasher hasher;
    auto const& hashFields = transaction.data;

    int32_t version = boost::endian::native_to_big((int32_t)hashFields.version);
    hasher.update(version);
    hasher.update(hashFields.chainID);
    hasher.update(hashFields.groupID);
    int64_t blockLimit = boost::endian::native_to_big((int64_t)hashFields.blockLimit);
    hasher.update(blockLimit);
    hasher.update(hashFields.nonce);
    hasher.update(hashFields.to);
    hasher.update(hashFields.input);
    hasher.update(hashFields.abi);

    decltype(transaction.dataHash) hash(Hasher::HASH_SIZE);
    hasher.final(out);
}

template <bcos::crypto::hasher::Hasher Hasher>
void impl_calculate(
    bcostars::TransactionReceipt const& receipt, bcos::concepts::bytebuffer::ByteBuffer auto& out)
{
    if (!receipt.dataHash.empty())
    {
        bcos::concepts::bytebuffer::assignTo(receipt.dataHash, out);
        return;
    }

    Hasher hasher;
    auto const& hashFields = receipt.data;
    int32_t version = boost::endian::native_to_big((int32_t)hashFields.version);
    hasher.update(version);
    hasher.update(hashFields.gasUsed);
    hasher.update(hashFields.contractAddress);
    int32_t status = boost::endian::native_to_big((int32_t)hashFields.status);
    hasher.update(status);
    hasher.update(hashFields.output);
    // vector<LogEntry> logEntries: 6
    for (auto const& log : hashFields.logEntries)
    {
        hasher.update(log.address);
        for (auto const& topicItem : log.topic)
        {
            hasher.update(topicItem);
        }
        hasher.update(log.data);
    }
    int64_t blockNumber = boost::endian::native_to_big((int64_t)hashFields.blockNumber);
    hasher.update(blockNumber);
    hasher.final(out);
}

template <bcos::crypto::hasher::Hasher Hasher>
auto impl_calculate(bcostars::Block const& block, bcos::concepts::bytebuffer::ByteBuffer auto& out)
{
    if (!block.blockHeader.dataHash.empty())
    {
        bcos::concepts::bytebuffer::assignTo(block.blockHeader.dataHash, out);
        return;
    }

    Hasher hasher;
    auto const& hashFields = block.blockHeader.data;

    int32_t version = boost::endian::native_to_big((int32_t)hashFields.version);
    hasher.update(version);
    for (auto const& parent : hashFields.parentInfo)
    {
        int64_t blockNumber = boost::endian::native_to_big((int64_t)parent.blockNumber);
        hasher.update(blockNumber);
        hasher.update(parent.blockHash);
    }
    hasher.update(hashFields.txsRoot);
    hasher.update(hashFields.receiptRoot);
    hasher.update(hashFields.stateRoot);

    int64_t number = boost::endian::native_to_big((int64_t)hashFields.blockNumber);
    hasher.update(number);
    hasher.update(hashFields.gasUsed);

    int64_t timestamp = boost::endian::native_to_big((int64_t)hashFields.timestamp);
    hasher.update(timestamp);

    int64_t sealer = boost::endian::native_to_big((int64_t)hashFields.sealer);
    hasher.update(sealer);

    for (auto const& nodeID : hashFields.sealerList)
    {
        hasher.update(nodeID);
    }
    // update extraData to hashBuffer: 12
    hasher.update(hashFields.extraData);
    // update consensusWeights to hashBuffer: 13
    for (auto weight : hashFields.consensusWeights)
    {
        int64_t networkWeight = boost::endian::native_to_big((int64_t)weight);
        hasher.update(networkWeight);
    }

    hasher.final(out);
}

}  // namespace bcos::concepts::hash
