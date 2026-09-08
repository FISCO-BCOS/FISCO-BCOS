// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0

// OpDepositEnvelopeTest — decodeDepositEnvelope is consensus-grade strict decoding with ~15
// rejection branches: one negative case per fail branch (exact-type BOOST_CHECK_THROW on
// OpTxValidationFailed) plus positive anchors decoding valid envelopes field-by-field.

#include <opstack-executor/OpDepositEncode.h>
#include <opstack-executor/OpstackExecutor.h>

#include <bcos-codec/rlp/Common.h>
#include <bcos-codec/rlp/RLPEncode.h>
#include <bcos-utilities/Common.h>
#include <boost/test/unit_test.hpp>
#include <evmc/evmc.hpp>

#include <cstdint>
#include <optional>

using bcos::executor_v1::opstack::decodeDepositEnvelope;
using bcos::executor_v1::opstack::OpTxValidationFailed;
namespace rlp = bcos::codec::rlp;

namespace
{
/// Canonical RLP string item for an arbitrary payload (empty -> 0x80; single byte < 0x80 bare).
bcos::bytes item(bcos::bytesConstRef payload)
{
    bcos::bytes out;
    rlp::encode(out, payload);
    return out;
}

// bytesConstRef is explicit-only from bytes; this overload keeps call sites terse.
bcos::bytes item(bcos::bytes const& payload)
{
    return item(bcos::bytesConstRef{payload.data(), payload.size()});
}

/// Canonical RLP integer item (0 -> the empty item 0x80).
bcos::bytes intItem(uint64_t v)
{
    bcos::bytes out;
    rlp::encode(out, v);
    return out;
}

/// `0x7e || rlp(list)` envelope around an already-encoded list payload.
bcos::bytes envelope(bcos::bytesConstRef listPayload)
{
    bcos::bytes out{static_cast<bcos::byte>(0x7e)};
    rlp::encodeHeader(out, {.isList = true, .payloadLength = listPayload.size()});
    out.insert(out.end(), listPayload.begin(), listPayload.end());
    return out;
}

bcos::bytes envelope(bcos::bytes const& listPayload)
{
    return envelope(bcos::bytesConstRef{listPayload.data(), listPayload.size()});
}

bcos::bytesConstRef optRef(std::optional<bcos::bytes> const& v)
{
    return v.has_value() ? bcos::bytesConstRef{v->data(), v->size()} : bcos::bytesConstRef{};
}

struct Fields
{
    bcos::bytes sourceHash = bcos::bytes(32, 0x11);
    bcos::bytes from = bcos::bytes(20, 0x22);
    std::optional<bcos::bytes> to = bcos::bytes(20, 0x33);
    std::optional<bcos::bytes> mint = bcos::bytes{0x12, 0x34};
    bcos::bytes value{0x01};  // minimal big-endian; empty = zero
    uint64_t gas = 1000000;
    uint64_t isSystemTx = 1;
    bcos::bytes data{0xde, 0xad};
};

bcos::bytes payloadOf(Fields const& f)
{
    bcos::bytes p;
    p = item(f.sourceHash);
    auto from = item(f.from);
    p.insert(p.end(), from.begin(), from.end());
    auto to = item(optRef(f.to));
    p.insert(p.end(), to.begin(), to.end());
    auto mint = item(optRef(f.mint));
    p.insert(p.end(), mint.begin(), mint.end());
    auto value = item(f.value);
    p.insert(p.end(), value.begin(), value.end());
    auto gas = intItem(f.gas);
    p.insert(p.end(), gas.begin(), gas.end());
    auto sys = intItem(f.isSystemTx);
    p.insert(p.end(), sys.begin(), sys.end());
    auto data = item(f.data);
    p.insert(p.end(), data.begin(), data.end());
    return p;
}

bcos::bytes validEnvelope(Fields const& f = {})
{
    return envelope(payloadOf(f));
}

void expectReject(bcos::bytes const& env)
{
    BOOST_CHECK_THROW((void)decodeDepositEnvelope(bcos::bytesConstRef{env.data(), env.size()}),
        OpTxValidationFailed);
}
}  // namespace

BOOST_AUTO_TEST_SUITE(OpDepositEnvelopeTest)

BOOST_AUTO_TEST_CASE(ValidEnvelopeDecodesFieldByField)
{
    Fields f;
    auto const env = validEnvelope(f);
    auto dep = decodeDepositEnvelope(bcos::bytesConstRef{env.data(), env.size()});

    for (size_t i = 0; i < 32; ++i)
        BOOST_CHECK_EQUAL(dep.source_hash.bytes[i], 0x11);
    BOOST_REQUIRE(dep.to.has_value());
    for (size_t i = 0; i < 20; ++i)
    {
        BOOST_CHECK_EQUAL(dep.from.bytes[i], 0x22);
        BOOST_CHECK_EQUAL(dep.to->bytes[i], 0x33);
    }
    BOOST_REQUIRE(dep.mint.has_value());
    BOOST_CHECK(*dep.mint == intx::uint256{0x1234});
    BOOST_CHECK(dep.value == intx::uint256{1});
    BOOST_CHECK_EQUAL(dep.gas_limit, 1000000);
    BOOST_CHECK(dep.is_system_tx);
    BOOST_REQUIRE_EQUAL(dep.data.size(), 2u);
    BOOST_CHECK_EQUAL(dep.data[0], 0xde);
    BOOST_CHECK_EQUAL(dep.data[1], 0xad);
}

BOOST_AUTO_TEST_CASE(BareByteIntegersDecode)
{
    // Canonical RLP integers 0x01..0x7f are a single bare byte. integerPayloadLength
    // must report 1 for those (0 is reserved for the empty item 0x80). Callers today
    // only use the result as an upper bound, so this pins the name/value contract
    // against a future exact-width check collapsing a bare byte with canonical zero.
    Fields f;
    f.mint = bcos::bytes{0x7f};
    f.value = bcos::bytes{0x01};
    f.gas = 21;
    f.isSystemTx = 1;
    auto const env = validEnvelope(f);
    auto dep = decodeDepositEnvelope(bcos::bytesConstRef{env.data(), env.size()});

    BOOST_REQUIRE(dep.mint.has_value());
    BOOST_CHECK(*dep.mint == intx::uint256{0x7f});
    BOOST_CHECK(dep.value == intx::uint256{1});
    BOOST_CHECK_EQUAL(dep.gas_limit, 21);
    BOOST_CHECK(dep.is_system_tx);
}

BOOST_AUTO_TEST_CASE(ValidCreationEnvelopeEmptyMintZeroValue)
{
    Fields f;
    f.to = std::nullopt;    // empty item -> contract creation
    f.mint = std::nullopt;  // empty item -> no mint
    f.value = {};           // zero -> empty item
    f.gas = 21000;
    f.isSystemTx = 0;
    f.data = {};
    auto const env = validEnvelope(f);
    auto dep = decodeDepositEnvelope(bcos::bytesConstRef{env.data(), env.size()});

    BOOST_CHECK(!dep.to.has_value());
    BOOST_CHECK(!dep.mint.has_value());
    BOOST_CHECK(dep.value == intx::uint256{0});
    BOOST_CHECK_EQUAL(dep.gas_limit, 21000);
    BOOST_CHECK(!dep.is_system_tx);
    BOOST_CHECK(dep.data.empty());
}

BOOST_AUTO_TEST_CASE(RejectsEnvelopeShapeFaults)
{
    expectReject({});      // empty
    expectReject({0x7e});  // type byte only (size < 2)
    {
        auto env = validEnvelope();
        env[0] = 0x02;  // not a 0x7e deposit
        expectReject(env);
    }
    expectReject({0x7e, 0x80});  // body is a string item, not a list
    {
        auto env = validEnvelope();
        env.push_back(0x00);  // trailing bytes after the RLP list
        expectReject(env);
    }
    {
        auto env = validEnvelope();
        env.pop_back();  // truncated: list payload longer than the body
        expectReject(env);
    }
}

BOOST_AUTO_TEST_CASE(RejectsFieldFaults)
{
    // sourceHash wrong length (31 bytes) -> decodeItems fixed-length check
    {
        Fields f;
        f.sourceHash = bcos::bytes(31, 0x11);
        expectReject(envelope(payloadOf(f)));
    }
    // missing to field (list ends after from)
    {
        bcos::bytes p;
        auto sh = item(bcos::bytes(32, 0x11));
        auto fr = item(bcos::bytes(20, 0x22));
        p.insert(p.end(), sh.begin(), sh.end());
        p.insert(p.end(), fr.begin(), fr.end());
        expectReject(envelope(p));
    }
    // missing mint field (list ends after to)
    {
        bcos::bytes p;
        auto sh = item(bcos::bytes(32, 0x11));
        auto fr = item(bcos::bytes(20, 0x22));
        auto to = item(bcos::bytes(20, 0x33));
        p.insert(p.end(), sh.begin(), sh.end());
        p.insert(p.end(), fr.begin(), fr.end());
        p.insert(p.end(), to.begin(), to.end());
        expectReject(envelope(p));
    }
    // to wrong length (19 bytes)
    {
        Fields f;
        f.to = bcos::bytes(19, 0x33);
        expectReject(envelope(payloadOf(f)));
    }
    // trailing item inside the list
    {
        auto p = payloadOf(Fields{});
        auto extra = intItem(1);
        p.insert(p.end(), extra.begin(), extra.end());
        expectReject(envelope(p));
    }
    // data as an RLP list (0xc0) — not a byte string
    {
        const Fields base;
        bcos::bytes p;
        auto sh = item(base.sourceHash);
        p.insert(p.end(), sh.begin(), sh.end());
        auto fr = item(base.from);
        p.insert(p.end(), fr.begin(), fr.end());
        auto to = item(*base.to);
        p.insert(p.end(), to.begin(), to.end());
        auto mint = item(*base.mint);
        p.insert(p.end(), mint.begin(), mint.end());
        auto value = item(base.value);
        p.insert(p.end(), value.begin(), value.end());
        auto gas = intItem(base.gas);
        p.insert(p.end(), gas.begin(), gas.end());
        auto sys = intItem(base.isSystemTx);
        p.insert(p.end(), sys.begin(), sys.end());
        p.push_back(0xc0);  // data field is an empty list
        expectReject(envelope(p));
    }
}

BOOST_AUTO_TEST_CASE(RejectsNonCanonicalOrOverWideIntegers)
{
    const Fields base;
    auto withMintPayload = [&](bcos::bytes const& mintItem) {
        bcos::bytes p;
        auto sh = item(base.sourceHash);
        p.insert(p.end(), sh.begin(), sh.end());
        auto fr = item(base.from);
        p.insert(p.end(), fr.begin(), fr.end());
        auto to = item(*base.to);
        p.insert(p.end(), to.begin(), to.end());
        p.insert(p.end(), mintItem.begin(), mintItem.end());  // raw, possibly non-canonical
        auto value = item(base.value);
        p.insert(p.end(), value.begin(), value.end());
        auto gas = intItem(base.gas);
        p.insert(p.end(), gas.begin(), gas.end());
        auto sys = intItem(base.isSystemTx);
        p.insert(p.end(), sys.begin(), sys.end());
        auto data = item(base.data);
        p.insert(p.end(), data.begin(), data.end());
        return envelope(p);
    };

    // mint over-wide: 33-byte payload (0xa1 + 33 bytes, non-zero leading)
    {
        bcos::bytes wide(33, 0x07);
        expectReject(withMintPayload(item(wide)));
    }
    // mint non-canonical: integer zero as the bare byte 0x00 (canonical zero is 0x80)
    expectReject(withMintPayload({0x00}));
    // mint non-canonical: multi-byte payload with a leading zero byte
    expectReject(withMintPayload({0x82, 0x00, 0x01}));
    // mint non-canonical: single-byte payload < 0x80 in short-string form (0x81 0x01)
    expectReject(withMintPayload({0x81, 0x01}));
    // mint over-wide in long-string form would need >= 56 bytes; 33B short form above suffices.

    // value over-wide (33 bytes)
    {
        Fields f = base;
        f.value = bcos::bytes(33, 0x07);
        expectReject(envelope(payloadOf(f)));
    }
    // gas over-wide (9 bytes)
    {
        bcos::bytes p;
        auto add = [&p](bcos::bytes const& b) {
            auto it = item(b);
            p.insert(p.end(), it.begin(), it.end());
        };
        add(base.sourceHash);
        add(base.from);
        add(*base.to);
        add(*base.mint);
        add(base.value);
        add(bcos::bytes(9, 0x07));  // gas, 9 bytes
        auto sys = intItem(base.isSystemTx);
        p.insert(p.end(), sys.begin(), sys.end());
        add(base.data);
        expectReject(envelope(p));
    }
    // gas = 2^64-1 (8 bytes, canonical width) but above INT64_MAX
    {
        Fields f = base;
        f.gas = 0;  // placeholder; raw item below
        bcos::bytes p;
        auto add = [&p](bcos::bytes const& b) {
            auto it = item(b);
            p.insert(p.end(), it.begin(), it.end());
        };
        add(base.sourceHash);
        add(base.from);
        add(*base.to);
        add(*base.mint);
        add(base.value);
        add(bcos::bytes(8, 0xff));  // gas = 0xFFFFFFFFFFFFFFFF
        auto sys = intItem(base.isSystemTx);
        p.insert(p.end(), sys.begin(), sys.end());
        add(base.data);
        expectReject(envelope(p));
    }
    // gas in non-canonical long form (0xb8 0x02 ...): passes the width gate, rejected by
    // decodeHeader's long-form < 56 canonicality check
    {
        bcos::bytes p;
        auto add = [&p](bcos::bytes const& b) {
            auto it = item(b);
            p.insert(p.end(), it.begin(), it.end());
        };
        add(base.sourceHash);
        add(base.from);
        add(*base.to);
        add(*base.mint);
        add(base.value);
        p.insert(p.end(), {0xb8, 0x02, 0x01, 0x01});  // gas = 257 in long form
        auto sys = intItem(base.isSystemTx);
        p.insert(p.end(), sys.begin(), sys.end());
        add(base.data);
        expectReject(envelope(p));
    }
    // gas as an RLP list (0xc0) — not an integer at all
    {
        bcos::bytes p;
        auto add = [&p](bcos::bytes const& b) {
            auto it = item(b);
            p.insert(p.end(), it.begin(), it.end());
        };
        add(base.sourceHash);
        add(base.from);
        add(*base.to);
        add(*base.mint);
        add(base.value);
        p.push_back(0xc0);  // gas field is an empty list
        auto sys = intItem(base.isSystemTx);
        p.insert(p.end(), sys.begin(), sys.end());
        add(base.data);
        expectReject(envelope(p));
    }
    // isSystemTx value 2 (only 0/1 accepted — op-geth decodeBool)
    {
        Fields f = base;
        f.isSystemTx = 2;
        expectReject(envelope(payloadOf(f)));
    }
    // isSystemTx over-wide (9 bytes)
    {
        bcos::bytes p;
        auto add = [&p](bcos::bytes const& b) {
            auto it = item(b);
            p.insert(p.end(), it.begin(), it.end());
        };
        add(base.sourceHash);
        add(base.from);
        add(*base.to);
        add(*base.mint);
        add(base.value);
        auto gas = intItem(base.gas);
        p.insert(p.end(), gas.begin(), gas.end());
        add(bcos::bytes(9, 0x01));  // isSystemTx, 9 bytes
        add(base.data);
        expectReject(envelope(p));
    }
}

// encodeDepositEnvelope must produce the canonical op-geth 0x7e deposit envelope bytes —
// the deposit tx root commits these bytes, so the encoder is consensus-critical. The golden
// vector pins the exact output; the round-trip through decodeDepositEnvelope proves the
// pair are inverses on every field.
BOOST_AUTO_TEST_CASE(EncodeDepositEnvelopeGolden)
{
    bcos::evm::opstack::DepositTx dep{};
    std::fill(std::begin(dep.source_hash.bytes), std::end(dep.source_hash.bytes), 0x11);
    std::fill(std::begin(dep.from.bytes), std::end(dep.from.bytes), 0x22);
    dep.to = std::nullopt;    // contract creation
    dep.mint = std::nullopt;  // no mint
    dep.value = 0;
    dep.gas_limit = 100000;
    dep.is_system_tx = false;
    // data stays empty

    auto const encoded = bcos::evm::opstack::encodeDepositEnvelope(dep);
    // 7e || f8 3f (long-form list header, 63-byte payload) || a0(source_hash 0x11×32)
    // || 94(from 0x22×20) || 80(to) 80(mint) 80(value) || 83 01 86 a0(gas 100000) || 80 80.
    BOOST_CHECK_EQUAL(bcos::toHex(encoded),
        "7ef83fa0111111111111111111111111111111111111111111111111111111111111111194"
        "2222222222222222222222222222222222222222808080830186a08080");

    // Round-trip: the decoder must reproduce every field.
    auto const decoded = decodeDepositEnvelope(bcos::bytesConstRef{encoded.data(), encoded.size()});
    BOOST_REQUIRE(!decoded.to.has_value());
    BOOST_REQUIRE(!decoded.mint.has_value());
    BOOST_CHECK(decoded.value == intx::uint256{0});
    BOOST_CHECK_EQUAL(decoded.gas_limit, 100000);
    BOOST_CHECK(!decoded.is_system_tx);
    BOOST_REQUIRE(decoded.data.empty());
    for (size_t i = 0; i < 32; ++i)
        BOOST_CHECK_EQUAL(decoded.source_hash.bytes[i], 0x11);
    for (size_t i = 0; i < 20; ++i)
        BOOST_CHECK_EQUAL(decoded.from.bytes[i], 0x22);
}

BOOST_AUTO_TEST_SUITE_END()
