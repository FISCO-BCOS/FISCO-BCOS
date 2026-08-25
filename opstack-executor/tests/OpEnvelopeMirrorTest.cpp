// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0

// OpEnvelopeMirrorTest — the envelope is authoritative over the tars mirror (review findings
// A + C): the chainId gate and the execution-fields cross-check must key on the SIGNED
// envelope, never the forgeable mirror. Tests call the gate/cross-check helpers directly
// with a FakeTransaction whose envelope bytes and mirror fields can be set independently,
// and pin the rejection messages so a deleted gate fails the test.

#include <opstack-executor/OpstackExecutor.h>

#include <bcos-codec/rlp/RLPEncode.h>
#include <bcos-utilities/Common.h>
#include <boost/test/unit_test.hpp>

#include <cstdint>
#include <optional>
#include <string>

using bcos::executor_v1::opstack::envelopeChainIdMismatch;
using bcos::executor_v1::opstack::envelopeExecutionFieldsMismatch;
namespace rlp = bcos::codec::rlp;

namespace
{
class FakeTx : public bcos::protocol::Transaction
{
public:
    uint8_t m_kind = 2;
    bcos::bytes m_input;
    int64_t m_gasLimit = 5000000;
    std::optional<bcos::u256> m_gasPrice;
    std::optional<bcos::u256> m_maxFeePerGas;
    std::optional<bcos::u256> m_maxPriorityFeePerGas;
    std::optional<bcos::u256> m_maxFeePerBlobGas;
    std::string m_sender = std::string(sizeof(evmc_address), '\xaa');
    std::string m_to;  // hex form ("0x" + 40 hex chars), or empty = creation
    bcos::u256 m_value = 0;
    std::string m_chainId = "10";  // DECIMAL string (mirror)
    std::string m_nonce = "0x7";   // hex quantity
    bcos::bytes m_extraBytes;

    uint8_t web3TypedTxKind() const override { return m_kind; }
    bcos::bytesConstRef input() const override
    {
        return bcos::bytesConstRef{m_input.data(), m_input.size()};
    }
    int64_t gasLimit() const override { return m_gasLimit; }
    std::optional<bcos::u256> gasPrice() const override { return m_gasPrice; }
    std::optional<bcos::u256> maxFeePerGas() const override { return m_maxFeePerGas; }
    std::optional<bcos::u256> maxPriorityFeePerGas() const override
    {
        return m_maxPriorityFeePerGas;
    }
    std::optional<bcos::u256> maxFeePerBlobGas() const override { return m_maxFeePerBlobGas; }
    std::string_view sender() const override { return m_sender; }
    std::string_view to() const override { return m_to; }
    bcos::u256 value() const override { return m_value; }
    std::string_view chainId() const override { return m_chainId; }
    std::string_view nonce() const override { return m_nonce; }
    bcos::bytesConstRef extraTransactionBytes() const override
    {
        return bcos::bytesConstRef{m_extraBytes.data(), m_extraBytes.size()};
    }
    std::optional<uint64_t> web3ChainIdFromEnvelope() const override
    {
        return bcos::rlp::protocol::web3ChainIdFromEnvelope(extraTransactionBytes());
    }

    // ---- unused stubs ----
    void decode(bcos::bytesConstRef) override {}
    void encode(bcos::bytes&) const override {}
    bcos::crypto::HashType hash() const override { return {}; }
    int32_t version() const override { return 0; }
    std::string_view groupId() const override { return {}; }
    int64_t blockLimit() const override { return 0; }
    void setNonce(std::string) override {}
    std::string_view abi() const override { return {}; }
    bcos::bytesConstRef extension() const override { return {}; }
    std::string_view extraData() const override { return {}; }
    int64_t importTime() const override { return 0; }
    void setImportTime(int64_t) override {}
    uint8_t type() const override { return 1; }  // Web3Transaction
    void forceSender(const bcos::bytes&) override {}
    void clearSenderAndHash() override {}
    void calculateHash(const bcos::crypto::Hash&) override {}
    bcos::bytesConstRef signatureData() const override { return {}; }
    int32_t attribute() const override { return 0; }
    void setAttribute(int32_t) override {}
};

/// Build a canonical EIP-1559 (0x02) envelope: 0x02 || rlp([chainId, nonce, prio, maxFee,
/// gasLimit, to, value, data, accessList]).
bcos::bytes eip1559Envelope(uint64_t chainId, uint64_t nonce, uint64_t gasLimit,
    std::string_view toHex, bcos::u256 value, bcos::bytes const& data)
{
    auto item = [](bcos::bytes const& payload) {
        bcos::bytes out;
        rlp::encode(out, bcos::bytesConstRef{payload.data(), payload.size()});
        return out;
    };
    auto intItem = [](uint64_t v) {
        bcos::bytes out;
        rlp::encode(out, v);
        return out;
    };
    bcos::bytes payload;
    auto append = [&payload](
                      bcos::bytes const& b) { payload.insert(payload.end(), b.begin(), b.end()); };
    append(intItem(chainId));
    append(intItem(nonce));
    append(intItem(30000000000));
    append(intItem(30000000000));
    append(intItem(gasLimit));
    auto toBytes = bcos::fromHex(toHex.substr(2));
    append(item(toBytes));
    // Test values are small; encode the u256 as an RLP integer via its low 64 bits.
    append(intItem(static_cast<uint64_t>(value)));
    append(item(data));
    payload.push_back(0xc0);  // empty accessList

    bcos::bytes out{static_cast<bcos::byte>(0x02)};
    rlp::encodeHeader(out, {.isList = true, .payloadLength = payload.size()});
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(OpEnvelopeMirrorSuite)

// The chainId gate reads the ENVELOPE, never the mirror: mirror says "10" but the envelope
// chainId is 9 — must reject with the pinned message, so deleting the gate fails the test.
BOOST_AUTO_TEST_CASE(EnvelopeChainIdWinsOverMirror)
{
    FakeTx tx;
    tx.m_extraBytes = eip1559Envelope(
        9, 7, 5000000, "0x811a752c8cd697e3cb27279c330ed1ada745a8d7", bcos::u256{5}, {});
    tx.m_chainId = "10";  // mirror claims the node chainId
    auto const gate = envelopeChainIdMismatch(tx, 10);
    BOOST_REQUIRE(gate.has_value());
    BOOST_CHECK(std::string(*gate).find("envelope chain_id 9 does not match node chainId 10") !=
                std::string::npos);
}

// Matching envelope chainId passes the gate even when the mirror differs.
BOOST_AUTO_TEST_CASE(EnvelopeChainIdMatchesNode)
{
    FakeTx tx;
    tx.m_extraBytes = eip1559Envelope(
        10, 7, 5000000, "0x811a752c8cd697e3cb27279c330ed1ada745a8d7", bcos::u256{5}, {});
    tx.m_chainId = "999";  // mirror is wrong but irrelevant
    BOOST_CHECK(!envelopeChainIdMismatch(tx, 10).has_value());
}

// A forged mirror value must be rejected by the execution-fields cross-check.
BOOST_AUTO_TEST_CASE(MirrorValueDivergenceRejected)
{
    FakeTx tx;
    tx.m_extraBytes = eip1559Envelope(
        10, 7, 5000000, "0x811a752c8cd697e3cb27279c330ed1ada745a8d7", bcos::u256{5}, {});
    tx.m_to = "0x811a752c8cd697e3cb27279c330ed1ada745a8d7";
    tx.m_nonce = "0x7";
    tx.m_gasLimit = 5000000;
    tx.m_value = bcos::u256{999};  // mirror forged (everything else consistent)
    auto const mismatch = envelopeExecutionFieldsMismatch(tx);
    BOOST_REQUIRE(mismatch.has_value());
    BOOST_CHECK(std::string(*mismatch).find("value mismatch") != std::string::npos);
}

// A forged mirror `to` must be rejected.
BOOST_AUTO_TEST_CASE(MirrorToDivergenceRejected)
{
    FakeTx tx;
    tx.m_extraBytes = eip1559Envelope(
        10, 7, 5000000, "0x811a752c8cd697e3cb27279c330ed1ada745a8d7", bcos::u256{5}, {});
    tx.m_to = "0x811a752c8cd697e3cb27279c330ed1ada745a8d8";  // last byte flipped
    BOOST_CHECK(envelopeExecutionFieldsMismatch(tx).has_value());
}

// Consistent mirror + envelope passes the cross-check.
BOOST_AUTO_TEST_CASE(ConsistentMirrorPasses)
{
    FakeTx tx;
    tx.m_extraBytes = eip1559Envelope(
        10, 7, 5000000, "0x811a752c8cd697e3cb27279c330ed1ada745a8d7", bcos::u256{5}, {0xde, 0xad});
    tx.m_value = bcos::u256{5};
    tx.m_to = "0x811a752c8cd697e3cb27279c330ed1ada745a8d7";
    tx.m_nonce = "0x7";
    tx.m_gasLimit = 5000000;
    tx.m_input = {0xde, 0xad};
    auto const m = envelopeExecutionFieldsMismatch(tx);
    if (m)
    {
        std::fprintf(stderr, "DEBUG consistent mismatch: %s\n", m->c_str());
        std::fprintf(stderr, "DEBUG envelope hex: %s\n", bcos::toHex(tx.m_extraBytes).c_str());
    }
    BOOST_CHECK(!m.has_value());
}

BOOST_AUTO_TEST_SUITE_END()
