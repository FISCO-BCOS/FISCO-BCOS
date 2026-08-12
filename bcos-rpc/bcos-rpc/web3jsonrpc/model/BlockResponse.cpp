#include "BlockResponse.h"
#include "Log.h"

#include <bcos-utilities/Bloom.h>

#include <range/v3/view/enumerate.hpp>

bcos::crypto::HashType bcos::rpc::opAwareBlockHash(const bcos::protocol::BlockHeader& header)
{
    // OP headers (Isthmus+ always present `withdrawalsRoot`; FISCO non-OP headers never do) hash
    // as keccak(encodeOpHeader) with the post-merge protocol constants — the hash the OP block
    // tables (`s_number_2_hash`) and op-node agree on — instead of the tars `dataHash` fallback
    // (which the read path re-derives as a FISCO tars hash). The three constants mirror
    // `bcos::engine::detail::opHeaderConst()` (engine must not be included from bcos-rpc; these
    // are the same golden-anchored bytes the engine pins).
    if (!header.withdrawalsRoot().has_value())
    {
        return header.hash();
    }
    bcos::protocol::BlockHeader::OpHeaderConst opConst{
        .ommersHash = bcos::h256{
            "0x1dcc4de8dec75d7aab85b567b6ccd41ad312451b948a7413f0a142fd40d49347"},
        .difficulty = bcos::u256(0),
        .nonce = bcos::h64{"0x0000000000000000"},
    };
    return header.opHeaderHash(opConst);
}

void bcos::rpc::combineBlockResponse(
    Json::Value& result, const bcos::protocol::Block& block, bool fullTxs)
{
    auto blockHeader = block.blockHeader();
    auto blockNumber = blockHeader->number();
    bcos::crypto::HashType blockHash = opAwareBlockHash(*blockHeader);
    bool const isOpHeader = blockHeader->withdrawalsRoot().has_value();
    result["number"] = toQuantity(blockNumber);
    result["hash"] = blockHash.hexPrefixed();
    result["parentHash"] = blockHeader->parentInfo().blockHash.hexPrefixed();
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
    result["logsBloom"] = toPaddingHexStringWithPrefix(BloomBytesSize, block.logsBloom());
    result["transactionsRoot"] = blockHeader->txsRoot().hexPrefixed();
    result["stateRoot"] = blockHeader->stateRoot().hexPrefixed();
    result["receiptsRoot"] = blockHeader->receiptsRoot().hexPrefixed();
    if (isOpHeader)
    {
        // OP: coinbase is the fee recipient (payload.suggestedFeeRecipient, stored on the header).
        auto coinbase = blockHeader->coinbase();
        result["miner"] = toHexStringWithPrefix(bcos::bytes(coinbase.begin(), coinbase.end()));
    }
    else if (std::cmp_greater(blockHeader->sealerList().size(), blockHeader->sealer()))
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
    // Real header gas limit (the OP sequencer pins it from the chain config; the previous
    // hardcoded 30M was wrong for OP chains with tx_gas_limit=3e9).
    result["gasLimit"] = toQuantity((uint64_t)blockHeader->gasLimit());
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
    // OP: prevRandao is real (Isthmus+ payload carries it); non-OP stays zero.
    result["mixHash"] = isOpHeader ? blockHeader->prevRandao().hexPrefixed() :
                                     crypto::HashType().hexPrefixed();
    // OP: baseFee is real (Isthmus+ payload.baseFeePerGas); non-OP has no carrier, keep 0x0.
    result["baseFeePerGas"] = (isOpHeader && blockHeader->baseFee().has_value()) ?
                                  toQuantity(*blockHeader->baseFee()) :
                                  "0x0";
    result["withdrawals"] = Json::Value(Json::arrayValue);
    // OP: empty withdrawals trie root (the OP header always carries it); non-OP keep zero.
    result["withdrawalsRoot"] = (isOpHeader && blockHeader->withdrawalsRoot().has_value()) ?
                                    blockHeader->withdrawalsRoot()->hexPrefixed() :
                                    crypto::HashType().hexPrefixed();
    // OP Jovian: blobGasUsed == da_footprint; else 0.
    result["blobGasUsed"] = (isOpHeader && blockHeader->blobGasUsed().has_value()) ?
                                toQuantity(*blockHeader->blobGasUsed()) :
                                "0x0";
    result["excessBlobGas"] = "0x0";
    result["parentBeaconBlockRoot"] = crypto::HashType().hexPrefixed();
}
