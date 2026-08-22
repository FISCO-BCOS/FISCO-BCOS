#include "BlockResponse.h"
#include "Log.h"

#include <bcos-rlp-protocol/EthBlockHeader.h>
#include <bcos-utilities/Bloom.h>

#include <range/v3/view/enumerate.hpp>

bcos::crypto::HashType bcos::rpc::opAwareBlockHash(const bcos::protocol::BlockHeader& header)
{
    if (!header.withdrawalsRoot().has_value())
    {
        return header.hash();
    }
    return bcos::protocol::EthBlockHeader::computeHash(header);
}

void bcos::rpc::combineBlockResponse(
    Json::Value& result, const bcos::protocol::Block& block, bool fullTxs)
{
    auto blockHeader = block.blockHeader();
    auto blockHash = opAwareBlockHash(*blockHeader);
    auto blockNumber = blockHeader->number();
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
    // Engine-built blocks (OP sequencer path) have an empty sealerList — the miner/coinbase
    // is the execution payload's feeRecipient, which is stored in the header's coinbase()
    // field (set by rebuildOpEthHeader from the payload attributes). Clients like alloy/cast
    // require this field for deserialization; without it eth_getBlockByNumber fails.
    if (!result.isMember("miner"))
    {
        auto coinbase = blockHeader->coinbase();
        auto coinbaseString = coinbase.hex();
        auto coinbaseHash = crypto::keccak256Hash(bytesConstRef(coinbaseString)).hex();
        toChecksumAddress(coinbaseString, coinbaseHash);
        result["miner"] = "0x" + coinbaseString;
    }
    result["difficulty"] = "0x0";
    result["totalDifficulty"] = "0x0";
    result["extraData"] = toHexStringWithPrefix(blockHeader->extraData());
    result["size"] = toQuantity(block.size());
    // gasLimit: engine-built/genesis blocks carry the real limit in the header (OP newPayload
    // via rebuildOpEthHeader, genesis via applyEthGenesisHeader); PBFT blocks never write it
    // (tars field empty -> u256(0)) and keep the legacy 30M so their output is byte-identical.
    auto gasLimit = blockHeader->gasLimit();
    result["gasLimit"] = (gasLimit == 0) ? toQuantity(30000000ULL) : toQuantity(gasLimit);
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
    result["mixHash"] = crypto::HashType().hexPrefixed();
    // baseFeePerGas: OP headers (engine-built / newPayload-rebuilt) carry the real EIP-1559
    // value; PBFT headers never write the tars field (nullopt, or 0) and keep the legacy 0x0
    // so their output stays byte-identical — same pattern as gasLimit above.
    if (auto baseFee = blockHeader->baseFee(); baseFee.has_value() && *baseFee != 0)
    {
        result["baseFeePerGas"] = toQuantity(*baseFee);
    }
    else
    {
        result["baseFeePerGas"] = "0x0";
    }
    result["withdrawals"] = Json::Value(Json::arrayValue);
    // Isthmus+: the header carries the MessagePasser storage root as withdrawalsRoot (the OP
    // semantic — set by the executed seal and rebuilt by the engine); pre-Isthmus/PBFT headers
    // have no value (nullopt) and keep the legacy zero.
    if (auto withdrawalsRoot = blockHeader->withdrawalsRoot(); withdrawalsRoot.has_value())
    {
        result["withdrawalsRoot"] = withdrawalsRoot->hexPrefixed();
    }
    else
    {
        result["withdrawalsRoot"] = crypto::HashType().hexPrefixed();
    }
    // blobGasUsed: pre-Jovian OP headers store 0 (identical output); Jovian reuses the header
    // slot for the DA footprint, which the RPC must surface. PBFT headers never write the
    // tars field (nullopt) and keep the legacy 0x0. excessBlobGas stays the constant 0: OP
    // Stack chains serve no blobs and no header field carries it.
    if (auto blobGasUsed = blockHeader->blobGasUsed(); blobGasUsed.has_value())
    {
        result["blobGasUsed"] = toQuantity(*blobGasUsed);
    }
    else
    {
        result["blobGasUsed"] = "0x0";
    }
    result["excessBlobGas"] = "0x0";
    // EIP-4788: pre-Cancun/PBFT headers have no PBBR (accessor returns nullopt when the
    // tars field is < 32 bytes) -> zero, symmetric with withdrawalsRoot above.
    if (auto parentBeaconRoot = blockHeader->parentBeaconBlockRoot(); parentBeaconRoot.has_value())
    {
        result["parentBeaconBlockRoot"] = parentBeaconRoot->hexPrefixed();
    }
    else
    {
        result["parentBeaconBlockRoot"] = crypto::HashType().hexPrefixed();
    }
    // EIP-7685 requestsHash: Isthmus+ OP headers carry sha256('') (set by the seal and rebuilt
    // by the engine); PBFT/pre-Isthmus headers have no value (nullopt) and keep the field
    // absent -- symmetric with op-geth (internal/ethapi/api.go:1092-1094, omitempty).
    if (auto requestsHash = blockHeader->requestsHash(); requestsHash.has_value())
    {
        result["requestsHash"] = requestsHash->hexPrefixed();
    }
}
