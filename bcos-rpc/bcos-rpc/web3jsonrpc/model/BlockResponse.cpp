#include "BlockResponse.h"
#include "Log.h"

#include <bcos-framework/protocol/Protocol.h>
#include <bcos-ledger/mpt/Constants.h>
#include <bcos-utilities/Bloom.h>

#include <range/v3/view/enumerate.hpp>

void bcos::rpc::combineBlockResponse(
    Json::Value& result, const bcos::protocol::Block& block, bool fullTxs)
{
    auto blockHeader = block.blockHeader();
    auto blockHash = blockHeader->hash();
    auto blockNumber = blockHeader->number();
    auto const ethVersion = blockHeader->ethBlockVersion();
    auto const isEth = ethVersion != bcos::protocol::EthBlockVersion::NON_ETH;

    result["number"] = toQuantity(blockNumber);
    result["hash"] = blockHash.hexPrefixed();
    // Genesis headers never carry parentInfo: BlockHeaderImpl::parentInfo() returns the
    // zero hash for an empty list, so reading it verbatim reproduces the historical
    // forced-zero behavior. This relies on every genesis build path leaving parentInfo
    // unset (Ledger.cpp documents this invariant); a future artifact that carries a
    // non-zero parent would surface here as a changed RPC value.
    result["parentHash"] = blockHeader->parentInfo().blockHash.hexPrefixed();

    if (isEth)
    {
        // Ethereum header: header-carried fields are taken from the header. The block-body
        // fields (withdrawals list, totalDifficulty) remain placeholders below.
        result["nonce"] = blockHeader->nonce().hexPrefixed();
        result["sha3Uncles"] = blockHeader->uncleHash().hexPrefixed();
        // EIP-55 checksummed address, matching geth's eth_getBlock* output.
        auto minerAddr = blockHeader->coinbase().hex();
        auto minerAddrHash = crypto::keccak256Hash(bytesConstRef(minerAddr)).hex();
        toChecksumAddress(minerAddr, minerAddrHash);
        result["miner"] = "0x" + minerAddr;
        result["difficulty"] = toQuantity(blockHeader->difficulty());
        result["mixHash"] = blockHeader->prevRandao().hexPrefixed();
    }
    else
    {
        // FISCO-BCOS header: fixed Ethereum-compatible empty values, miner derived from
        // the sealer list (FISCO headers carry no coinbase).
        result["nonce"] = "0x0000000000000000";
        // empty uncle hash: keccak256(RLP([]))
        result["sha3Uncles"] =
            "0x1dcc4de8dec75d7aab85b567b6ccd41ad312451b948a7413f0a142fd40d49347";
        result["difficulty"] = "0x0";
        result["mixHash"] = crypto::HashType().hexPrefixed();
        if (blockNumber == 0)
        {
            result["miner"] = "0x0000000000000000000000000000000000000000";
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
    }

    // logsBloom: the Eth branch reads the header's bloom (part of the RLP hash), the
    // NON_ETH branch the block's own bloom. A per-log copy was previously built here and
    // never read; removed.
    result["logsBloom"] = toPaddingHexStringWithPrefix(
        BloomBytesSize, isEth ? blockHeader->logsBloom() : block.logsBloom());
    result["transactionsRoot"] = blockHeader->txsRoot().hexPrefixed();
    result["stateRoot"] = blockHeader->stateRoot().hexPrefixed();
    result["receiptsRoot"] = blockHeader->receiptsRoot().hexPrefixed();
    result["totalDifficulty"] = "0x0";
    result["extraData"] = toHexStringWithPrefix(blockHeader->extraData());
    result["size"] = toQuantity(block.size());
    // Native FISCO-BCOS blocks never call setGasLimit, so the header field reads back as 0;
    // keep the historical fixed value for NON_ETH blocks (the Ethereum-compatible gas limit),
    // and the real header value for Eth blocks (where buildPayload always sets it).
    result["gasLimit"] = toQuantity(
        isEth ? blockHeader->gasLimit() : bcos::u256(30000000));
    result["gasUsed"] = toQuantity(static_cast<uint64_t>(blockHeader->gasUsed()));
    // BlockHeader stores the timestamp in milliseconds; the eth_* RPC emits seconds.
    result["timestamp"] = toQuantity(blockHeader->timestamp() / 1000);

    // Fork-gated Ethereum fields: only defined for the fork that introduced them. Eth blocks
    // read the values from the header; native NON_ETH blocks keep the historical fixed mock
    // values so their RPC shape is unchanged.
    auto versionAtLeast = [&](bcos::protocol::EthBlockVersion fork) {
        return static_cast<uint8_t>(ethVersion) >= static_cast<uint8_t>(fork);
    };
    if (isEth && versionAtLeast(bcos::protocol::EthBlockVersion::LONDON))
    {
        result["baseFeePerGas"] = toQuantity(blockHeader->baseFee().value_or(bcos::u256(0)));
    }
    if (isEth && versionAtLeast(bcos::protocol::EthBlockVersion::SHANGHAI))
    {
        // The withdrawals operation list is a block-body field and is not persisted in the
        // header (only its trie root is); emit the always-empty list so Shanghai-shaped
        // clients keep working, and the root verbatim from the header.
        result["withdrawals"] = Json::Value(Json::arrayValue);
        // geth emits withdrawalsRoot unconditionally for Shanghai+ blocks. The engine
        // build path always sets it (finalizeEthBlockHeader uses the empty-trie root), so
        // the absent case only arises for non-engine producers — emit the canonical
        // empty-trie root to keep the response shape unconditional for the fork.
        result["withdrawalsRoot"] =
            blockHeader->withdrawalsRoot().value_or(bcos::ledger::mpt::emptyRootHash())
                .hexPrefixed();
    }
    if (isEth && versionAtLeast(bcos::protocol::EthBlockVersion::CANCUN))
    {
        if (auto blobGasUsed = blockHeader->blobGasUsed())
        {
            result["blobGasUsed"] = toQuantity(*blobGasUsed);
        }
        if (auto excessBlobGas = blockHeader->excessBlobGas())
        {
            result["excessBlobGas"] = toQuantity(*excessBlobGas);
        }
        if (auto beaconRoot = blockHeader->parentBeaconBlockRoot())
        {
            result["parentBeaconBlockRoot"] = beaconRoot->hexPrefixed();
        }
    }
    if (isEth && versionAtLeast(bcos::protocol::EthBlockVersion::PRAGUE))
    {
        if (auto requestsHash = blockHeader->requestsHash())
        {
            result["requestsHash"] = requestsHash->hexPrefixed();
        }
    }
    if (!isEth)
    {
        // Native FISCO-BCOS blocks keep their pre-existing Ethereum-compatible mock shape.
        result["baseFeePerGas"] = "0x0";
        result["withdrawals"] = Json::Value(Json::arrayValue);
        // empty withdrawals trie root hash
        result["withdrawalsRoot"] = crypto::HashType().hexPrefixed();
        result["blobGasUsed"] = "0x0";
        result["excessBlobGas"] = "0x0";
        result["parentBeaconBlockRoot"] = crypto::HashType().hexPrefixed();
    }

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
