// OP payload builder: computes the transactionsRoot and blockHash the real node's
// engine_newPayloadV3 will validate, given a raw deposit envelope + the payload's
// header-relevant fields. Reuses the proven in-process machinery (computeOpTxRoot,
// BlockHeaderImpl::encodeOpHeader) instead of re-deriving the trie/RLP by hand.
//
// Input  (argv[1], JSON): all header-relevant payload fields + the deposit envelope.
// Output (stdout, JSON):  { transactionsRoot, blockHash }.
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-tars-protocol/protocol/BlockHeaderImpl.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <json/json.h>
#include <opstack-executor/OpEngineSeam.h>
#include <fstream>
#include <iostream>

namespace
{
bcos::bytes hexBytes(std::string const& h) { return bcos::fromHex(h); }

template <std::size_t N>
bcos::FixedBytes<N> hexFixed(std::string const& h)
{
    return bcos::FixedBytes<N>{std::string{h}};
}

bcos::u256 decU256(std::string const& d) { return bcos::u256(std::stoull(d)); }
}  // namespace

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::cerr << "usage: op-payload-builder <input.json>\n";
        return 1;
    }
    Json::Value in;
    {
        std::ifstream f(argv[1]);
        if (!f.is_open())
        {
            std::cerr << "cannot open " << argv[1] << "\n";
            return 1;
        }
        f >> in;
    }

    // 1. transactionsRoot = MPT over [raw envelope] (key = rlp(index), leaf = envelope as-is).
    std::vector<bcos::bytes> txs{hexBytes(in["envelope"].asString())};
    const auto txRoot = bcos::evm::engine::computeOpTxRoot(txs);

    // 2. Reconstruct the OP header (same 21 fields, same order as the engine's
    //    detail::rebuildOpEthHeader + encodeOpHeader). Timestamp is tars-stored in ms; the RLP
    //    encodes seconds (encodeOpHeader divides by 1000), so feed ms.
    bcostars::protocol::BlockHeaderImpl h;
    h.setParentInfo(bcos::protocol::ParentInfo{
        .blockNumber = 0,
        .blockHash = hexFixed<32>(in["parentHash"].asString()),
    });
    h.setCoinbase(hexFixed<20>(in["feeRecipient"].asString()));
    h.setStateRoot(hexFixed<32>(in["stateRoot"].asString()));
    h.setTxsRoot(txRoot);
    h.setReceiptsRoot(hexFixed<32>(in["receiptsRoot"].asString()));
    const bcos::bytes bloomBytes = hexBytes(in["logsBloom"].asString());
    h.setLogsBloom(bcos::bytesConstRef(bloomBytes.data(), bloomBytes.size()));
    h.setNumber(std::stoull(in["number"].asString()));
    h.setGasLimit(decU256(in["gasLimit"].asString()));
    h.setGasUsed(decU256(in["gasUsed"].asString()));
    h.setTimestamp(std::stoull(in["timestamp"].asString()) * 1000);
    h.setExtraData(hexBytes(in["extraData"].asString()));
    h.setPrevRandao(hexFixed<32>(in["prevRandao"].asString()));
    h.setBaseFee(decU256(in["baseFee"].asString()));
    h.setWithdrawalsRoot(hexFixed<32>(in["withdrawalsRoot"].asString()));
    h.setBlobGasUsed(decU256(in["blobGasUsed"].asString()));
    h.setExcessBlobGas(decU256(in["excessBlobGas"].asString()));
    h.setParentBeaconBlockRoot(hexFixed<32>(in["parentBeaconBlockRoot"].asString()));
    h.setRequestsHash(hexFixed<32>(in["requestsHash"].asString()));

    // 3. blockHash = keccak256(encodeOpHeader), with the 3 post-merge constants (byte-identical
    //    to engine's detail::opHeaderConst + OpBlockSeal.h).
    bcos::protocol::BlockHeader::OpHeaderConst c{
        .ommersHash = bcos::h256{
            std::string{"0x1dcc4de8dec75d7aab85b567b6ccd41ad312451b948a7413f0a142fd40d49347"}},
        .difficulty = bcos::u256(0),
        .nonce = bcos::h64{std::string{"0x0000000000000000"}},
    };
    const auto blockHash = h.opHeaderHash(c);

    Json::Value out;
    out["transactionsRoot"] = "0x" + txRoot.hex();
    out["blockHash"] = "0x" + blockHash.hex();
    std::cout << out.toStyledString();
    return 0;
}
