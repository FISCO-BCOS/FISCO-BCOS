#include "BlockResponse.h"
#include "Bloom.h"
#include "Log.h"
#include <range/v3/view/enumerate.hpp>

void bcos::rpc::combineBlockResponse(
    Json::Value& result, const bcos::protocol::Block& block, bool fullTxs)
{
    auto blockHeader = block.blockHeaderConst();
    auto blockHash = blockHeader->hash();
    auto blockNumber = blockHeader->number();
    result["number"] = toQuantity(blockNumber);
    result["hash"] = blockHash.hexPrefixed();
    // Only one parent block in BCOS. It is empty for genesis block
    for (const auto& info : blockHeader->parentInfo())
    {
        result["parentHash"] = info.blockHash.hexPrefixed();
    }
    result["nonce"] = "0x0000000000000000";
    // empty uncle hash: keccak256(RLP([]))
    result["sha3Uncles"] = "0x1dcc4de8dec75d7aab85b567b6ccd41ad312451b948a7413f0a142fd40d49347";
    rpc::Logs logs;
    for (auto receipt : block.receipts())
    {
        for (auto const& log : receipt->logEntries())
        {
            rpc::Log logObj{
                .address = bcos::bytes(log.address().begin(), log.address().end()),
                .topics = bcos::h256s(log.topics().begin(), log.topics().end()),
                .data = bcos::bytes(log.data().begin(), log.data().end()),
            };
            logs.push_back(std::move(logObj));
        }
    }
    auto logsBloom = getLogsBloom(logs);
    result["logsBloom"] = toHexStringWithPrefix(logsBloom);
    result["transactionsRoot"] = blockHeader->txsRoot().hexPrefixed();
    result["stateRoot"] = blockHeader->stateRoot().hexPrefixed();
    result["receiptsRoot"] = blockHeader->receiptsRoot().hexPrefixed();
    if (std::cmp_greater(blockHeader->sealerList().size(), blockHeader->sealer()))
    {
        auto pk = blockHeader->sealerList()[blockHeader->sealer()];
        auto hash = crypto::keccak256Hash(bcos::ref(pk));
        Address address = right160(hash);
        auto addrString = address.hex();
        auto addrHash = crypto::keccak256Hash(bytesConstRef(addrString)).hex();
        toChecksumAddress(addrString, addrHash);
        result["miner"] = "0x" + addrString;
    }
    // genesis block
    if (blockHeader->number() == 0)
    {
        result["miner"] = "0x0000000000000000000000000000000000000000";
        result["parentHash"] = "0x0000000000000000000000000000000000000000000000000000000000000000";
    }
    result["difficulty"] = "0x0";
    result["totalDifficulty"] = "0x0";
    result["extraData"] = toHexStringWithPrefix(blockHeader->extraData());
    result["size"] = toQuantity(block.size());
    // TODO: change it wen block gas limit apply
    result["gasLimit"] = toQuantity(30000000ULL);
    result["gasUsed"] = toQuantity((uint64_t)blockHeader->gasUsed());
    result["timestamp"] = toQuantity(blockHeader->timestamp() / 1000);  // to seconds
    if (fullTxs)
    {
        Json::Value txList = Json::arrayValue;
        for (auto [index, tx] : ::ranges::views::enumerate(block.transactions()))
        {
            Json::Value txJson = Json::objectValue;
            combineTxResponse(txJson, *tx, index, blockNumber, blockHash);
            txList.append(txJson);
        }
        result["transactions"] = std::move(txList);
    }
    else
    {
        Json::Value txHashesList = Json::arrayValue;
        for (auto hash : block.transactionHashes())
        {
            txHashesList.append(hash.hexPrefixed());
        }
        result["transactions"] = std::move(txHashesList);
    }
    result["uncles"] = Json::Value(Json::arrayValue);
}
