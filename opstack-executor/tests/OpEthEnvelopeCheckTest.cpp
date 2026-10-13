// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0

// OpEthEnvelopeCheckTest — coverage for the bcos-evm-free envelope↔mirror
// cross-checks (OpEnvelopeCheck.h, step 3.1.3). Subsumes the legacy
// OpEnvelopeMirrorTest scenario set (deleted in 3.3 with the old helpers in
// OpstackExecutor.h): per-type field binds, list-shaped field rejects, chainId
// gate (incl. unprotected preimage / v=27/28 / malformed-v forms), zero sender,
// blob/accessList/authorizationList binds, and the truncated-item bound checks.
// The mirror side is a FakeTx over protocol::Transaction accessors — the same
// accessors the new executor consumes; the envelope side is hand-built RLP, so
// the two sides can be diverged independently.

#include <opstack-executor/OpEnvelopeCheck.h>
#include <opstack-executor/OpEthL1Attributes.h>  // encodeOpEthDepositEnvelope (deposit chainId skip)

#include <bcos-codec/rlp/RLPEncode.h>
#include <bcos-rlp-protocol/Web3TxEnvelope.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/test/unit_test.hpp>

#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

using bcos::executor_v1::opstack::opEthBlockPathUnboundAuthorizationList;
using bcos::executor_v1::opstack::opEthBlockPathZeroSender;
using bcos::executor_v1::opstack::opEthEnvelopeChainIdMismatch;
using bcos::executor_v1::opstack::opEthEnvelopeExecutionFieldsMismatch;
namespace rlp = bcos::codec::rlp;

namespace
{
constexpr std::string_view kToHex = "0x811a752c8cd697e3cb27279c330ed1ada745a8d7";
constexpr uint64_t kNodeChainId = 10;
constexpr uint64_t k1559Fee = 30'000'000'000ULL;
constexpr uint64_t kLegacyGasPrice = 1'000'000'000ULL;

/// Byte-filled hash/address factories — FixedHash's string constructor parses
/// HEX, so raw byte patterns must be written through data() instead.
bcos::h256 hashFilledWith(uint8_t fill)
{
    bcos::h256 h{};
    std::memset(h.data(), fill, sizeof(h));
    return h;
}

bcos::Address addressFilledWith(uint8_t fill)
{
    bcos::Address a{};
    std::memset(a.data(), fill, sizeof(a));
    return a;
}

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
    std::string m_to{kToHex};  // hex form ("0x" + 40 hex chars), or empty = creation
    bcos::u256 m_value = 5;
    std::string m_chainId = "10";  // DECIMAL string (mirror)
    std::string m_nonce = "0x7";   // hex quantity
    bcos::bytes m_extraBytes;
    bcos::protocol::Web3AccessList m_accessList;
    bcos::protocol::VersionedHashes m_blobHashes;
    bcos::protocol::AuthorizationList m_authorizationList;

    /// Fill the mirror fee defaults to match the envelope builders below; a
    /// test that forges a fee sets the field AFTER calling this.
    void fillFeeDefaults()
    {
        if (m_kind == 0 || m_kind == 1)
        {
            if (!m_gasPrice.has_value())
                m_gasPrice = kLegacyGasPrice;
        }
        else
        {
            if (!m_maxFeePerGas.has_value())
                m_maxFeePerGas = k1559Fee;
            if (!m_maxPriorityFeePerGas.has_value())
                m_maxPriorityFeePerGas = k1559Fee;
            if (m_kind == 3 && !m_maxFeePerBlobGas.has_value())
                m_maxFeePerBlobGas = 1;
        }
    }

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
    bcos::protocol::Web3AccessList web3AccessList() const override { return m_accessList; }
    bcos::protocol::VersionedHashes blobVersionedHashes() const override { return m_blobHashes; }
    bcos::protocol::AuthorizationList authorizationList() const override
    {
        return m_authorizationList;
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

// ---- envelope builders (RLP hand-encoding, same recipes as OpEnvelopeMirrorTest) ----

/// Pins the convenience overload's contract: it must parse the envelope BYTES, never
/// the virtual web3ChainIdFromEnvelope (a polymorphic override must not create a
/// second consensus interpretation — legacy EnvelopeChainIdGateIgnoresDivergentVirtualOverride).
class DivergentChainIdTx : public FakeTx
{
public:
    std::optional<uint64_t> web3ChainIdFromEnvelope() const override { return kNodeChainId; }
};

bcos::bytes rlpString(bcos::bytes const& payload)
{
    bcos::bytes out;
    rlp::encode(out, bcos::bytesConstRef{payload.data(), payload.size()});
    return out;
}

bcos::bytes rlpInt(uint64_t v)
{
    bcos::bytes out;
    rlp::encode(out, v);
    return out;
}

bcos::bytes rlpWrapList(uint8_t typeByteOrNone, bcos::bytes const& payload, bool typed)
{
    bcos::bytes out;
    if (typed)
        out.push_back(static_cast<bcos::byte>(typeByteOrNone));
    rlp::encodeHeader(out, {.isList = true, .payloadLength = payload.size()});
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

/// 1-byte list 0xc1 0x05: payloadLength==1 passes the integer width guard, so the
/// kind check (not decode's UnexpectedList) is what must reject it.
bcos::bytes shortListItem()
{
    return {0xc1, 0x05};
}

/// legacy: rlp([nonce, gasPrice, gasLimit, to, value, data, v, r, s]).
bcos::bytes legacyEnvelope(uint64_t nonce, uint64_t gasLimit, std::string_view toHex,
    bcos::u256 value, bcos::bytes const& data, bool nonceIsList = false, bool gasIsList = false,
    bool valueIsList = false)
{
    bcos::bytes payload;
    auto append = [&payload](bcos::bytes const& b) {
        payload.insert(payload.end(), b.begin(), b.end());
    };
    append(nonceIsList ? shortListItem() : rlpInt(nonce));
    append(rlpInt(kLegacyGasPrice));
    append(gasIsList ? shortListItem() : rlpInt(gasLimit));
    auto toBytes = toHex.empty() ? bcos::bytes{} : bcos::fromHex(toHex.substr(2));
    append(rlpString(toBytes));
    append(valueIsList ? shortListItem() : rlpInt(static_cast<uint64_t>(value)));
    append(rlpString(data));
    append(rlpInt(27));  // v (unprotected legacy)
    append(rlpInt(1));   // r
    append(rlpInt(2));   // s
    return rlpWrapList(0, payload, false);
}

/// 0x01: 0x01 || rlp([chainId, nonce, gasPrice, gasLimit, to, value, data, accessList]).
bcos::bytes accessListEnvelope(uint64_t chainId, uint64_t nonce, uint64_t gasLimit,
    std::string_view toHex, bcos::u256 value, bcos::bytes const& data, bool nonceIsList = false,
    bool gasIsList = false, bool valueIsList = false)
{
    bcos::bytes payload;
    auto append = [&payload](bcos::bytes const& b) {
        payload.insert(payload.end(), b.begin(), b.end());
    };
    append(rlpInt(chainId));
    append(nonceIsList ? shortListItem() : rlpInt(nonce));
    append(rlpInt(kLegacyGasPrice));
    append(gasIsList ? shortListItem() : rlpInt(gasLimit));
    auto toBytes = toHex.empty() ? bcos::bytes{} : bcos::fromHex(toHex.substr(2));
    append(rlpString(toBytes));
    append(valueIsList ? shortListItem() : rlpInt(static_cast<uint64_t>(value)));
    append(rlpString(data));
    payload.push_back(0xc0);  // empty accessList
    return rlpWrapList(0x01, payload, true);
}

/// 0x02: 0x02 || rlp([chainId, nonce, prio, maxFee, gasLimit, to, value, data, accessList]).
bcos::bytes eip1559Envelope(uint64_t chainId, uint64_t nonce, uint64_t gasLimit,
    std::string_view toHex, bcos::u256 value, bcos::bytes const& data, bool toIsList = false,
    bool dataIsList = false, bool nonceIsList = false, bool gasIsList = false,
    bool valueIsList = false)
{
    bcos::bytes payload;
    auto append = [&payload](bcos::bytes const& b) {
        payload.insert(payload.end(), b.begin(), b.end());
    };
    append(rlpInt(chainId));
    append(nonceIsList ? shortListItem() : rlpInt(nonce));
    append(rlpInt(k1559Fee));
    append(rlpInt(k1559Fee));
    append(gasIsList ? shortListItem() : rlpInt(gasLimit));
    auto toBytes = toHex.empty() ? bcos::bytes{} : bcos::fromHex(toHex.substr(2));
    if (toIsList)
        payload.push_back(0xc0);
    else
        append(rlpString(toBytes));
    append(valueIsList ? shortListItem() : rlpInt(static_cast<uint64_t>(value)));
    if (dataIsList)
        payload.push_back(0xc0);
    else
        append(rlpString(data));
    payload.push_back(0xc0);  // empty accessList
    return rlpWrapList(0x02, payload, true);
}

/// 0x02 carrying the EIP-4844-in-1559 extension (14-item shape): maxFeePerBlobGas
/// at idx 9 and blobVersionedHashes at idx 10, then yParity/r/s.
bcos::bytes eip1559BlobExtEnvelope(uint64_t chainId, uint64_t nonce, uint64_t gasLimit,
    std::string_view toHex, bcos::u256 value, bcos::bytes const& data)
{
    bcos::bytes payload;
    auto append = [&payload](bcos::bytes const& b) {
        payload.insert(payload.end(), b.begin(), b.end());
    };
    append(rlpInt(chainId));
    append(rlpInt(nonce));
    append(rlpInt(k1559Fee));
    append(rlpInt(k1559Fee));
    append(rlpInt(gasLimit));
    auto toBytes = toHex.empty() ? bcos::bytes{} : bcos::fromHex(toHex.substr(2));
    append(rlpString(toBytes));
    append(rlpInt(static_cast<uint64_t>(value)));
    append(rlpString(data));
    payload.push_back(0xc0);  // empty accessList
    append(rlpInt(1));        // maxFeePerBlobGas (idx 9)
    payload.push_back(0xc0);  // empty blobVersionedHashes (idx 10)
    append(rlpInt(0));        // yParity
    append(rlpInt(1));        // r
    append(rlpInt(1));        // s
    return rlpWrapList(0x02, payload, true);
}

/// RLP-encode an EIP-2930 access list: [[address20, [keys32...]], ...].
bcos::bytes rlpAccessList(bcos::protocol::Web3AccessList const& list)
{
    bcos::bytes entries;
    for (auto const& entry : list)
    {
        bcos::bytes entryPayload = rlpString(
            bcos::bytes(entry.account.data(), entry.account.data() + entry.account.size()));
        bcos::bytes keysPayload;
        for (auto const& key : entry.storageKeys)
        {
            auto encoded = rlpString(bcos::bytes(key.data(), key.data() + key.size()));
            keysPayload.insert(keysPayload.end(), encoded.begin(), encoded.end());
        }
        bcos::bytes keys;
        rlp::encodeHeader(keys, {.isList = true, .payloadLength = keysPayload.size()});
        keys.insert(keys.end(), keysPayload.begin(), keysPayload.end());
        entryPayload.insert(entryPayload.end(), keys.begin(), keys.end());
        bcos::bytes entryRlp;
        rlp::encodeHeader(entryRlp, {.isList = true, .payloadLength = entryPayload.size()});
        entryRlp.insert(entryRlp.end(), entryPayload.begin(), entryPayload.end());
        entries.insert(entries.end(), entryRlp.begin(), entryRlp.end());
    }
    bcos::bytes out;
    rlp::encodeHeader(out, {.isList = true, .payloadLength = entries.size()});
    out.insert(out.end(), entries.begin(), entries.end());
    return out;
}

/// RLP-encode a list of 32-byte versioned hashes.
bcos::bytes rlpHashList(std::vector<bcos::h256> const& hashes)
{
    bcos::bytes payload;
    for (auto const& hash : hashes)
    {
        auto encoded = rlpString(bcos::bytes(hash.data(), hash.data() + hash.size()));
        payload.insert(payload.end(), encoded.begin(), encoded.end());
    }
    bcos::bytes out;
    rlp::encodeHeader(out, {.isList = true, .payloadLength = payload.size()});
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

/// 0x03 blob envelope: [chainId, nonce, prio, maxFee, gas, to, value, data, accessList,
/// maxFeePerBlobGas, blobVersionedHashes, yParity, r, s].
bcos::bytes blobEnvelope(uint64_t chainId, uint64_t nonce, uint64_t gasLimit,
    std::string_view toHex, bcos::u256 value, bcos::bytes const& data,
    std::vector<bcos::h256> const& blobHashes)
{
    bcos::bytes payload;
    auto append = [&payload](bcos::bytes const& b) {
        payload.insert(payload.end(), b.begin(), b.end());
    };
    append(rlpInt(chainId));
    append(rlpInt(nonce));
    append(rlpInt(k1559Fee));
    append(rlpInt(k1559Fee));
    append(rlpInt(gasLimit));
    auto toBytes = toHex.empty() ? bcos::bytes{} : bcos::fromHex(toHex.substr(2));
    append(rlpString(toBytes));
    append(rlpInt(static_cast<uint64_t>(value)));
    append(rlpString(data));
    payload.push_back(0xc0);  // empty accessList
    append(rlpInt(1));        // maxFeePerBlobGas
    append(rlpHashList(blobHashes));
    append(rlpInt(0));  // yParity
    append(rlpInt(1));  // r
    append(rlpInt(1));  // s
    return rlpWrapList(0x03, payload, true);
}

/// RLP-encode the authorization list: [[chainId, address20, nonce, yParity, r, s], ...].
bcos::bytes rlpAuthorizationList(bcos::protocol::AuthorizationList const& list)
{
    bcos::bytes entries;
    for (auto const& auth : list)
    {
        bcos::bytes tuple;
        auto append = [&tuple](bcos::bytes const& b) {
            tuple.insert(tuple.end(), b.begin(), b.end());
        };
        append(rlpInt(auth.chainId));
        append(rlpString(bcos::bytes(auth.address.data(), auth.address.data() + 20)));
        append(rlpInt(auth.nonce));
        append(rlpInt(auth.v));
        append(rlpString(bcos::toCompactBigEndian(auth.r)));
        append(rlpString(bcos::toCompactBigEndian(auth.s)));
        bcos::bytes tupleRlp;
        rlp::encodeHeader(tupleRlp, {.isList = true, .payloadLength = tuple.size()});
        tupleRlp.insert(tupleRlp.end(), tuple.begin(), tuple.end());
        entries.insert(entries.end(), tupleRlp.begin(), tupleRlp.end());
    }
    bcos::bytes out;
    rlp::encodeHeader(out, {.isList = true, .payloadLength = entries.size()});
    out.insert(out.end(), entries.begin(), entries.end());
    return out;
}

/// 0x04 set_code envelope: [chainId, nonce, prio, maxFee, gas, to, value, data, accessList,
/// authorizationList, yParity, r, s].
bcos::bytes setCodeEnvelope(uint64_t chainId, uint64_t nonce, uint64_t gasLimit,
    std::string_view toHex, bcos::u256 value, bcos::bytes const& data,
    bcos::protocol::AuthorizationList const& authList)
{
    bcos::bytes payload;
    auto append = [&payload](bcos::bytes const& b) {
        payload.insert(payload.end(), b.begin(), b.end());
    };
    append(rlpInt(chainId));
    append(rlpInt(nonce));
    append(rlpInt(k1559Fee));
    append(rlpInt(k1559Fee));
    append(rlpInt(gasLimit));
    auto toBytes = toHex.empty() ? bcos::bytes{} : bcos::fromHex(toHex.substr(2));
    append(rlpString(toBytes));
    append(rlpInt(static_cast<uint64_t>(value)));
    append(rlpString(data));
    payload.push_back(0xc0);  // empty accessList
    append(rlpAuthorizationList(authList));
    append(rlpInt(0));  // yParity
    append(rlpInt(1));  // r
    append(rlpInt(1));  // s
    return rlpWrapList(0x04, payload, true);
}

bcos::protocol::Authorization makeAuth(uint8_t tag)
{
    bcos::protocol::Authorization auth;
    auth.chainId = kNodeChainId;
    auth.address = addressFilledWith(static_cast<uint8_t>(0x30 + tag));
    auth.nonce = tag;
    auth.v = tag % 2;
    auth.r = bcos::u256(1000 + tag);
    auth.s = bcos::u256(2000 + tag);
    return auth;
}

/// The convenience pattern for the consistent case: build the envelope, fill
/// the mirror defaults, expect nullopt.
void expectConsistent(FakeTx& tx)
{
    tx.fillFeeDefaults();
    auto const err = opEthEnvelopeExecutionFieldsMismatch(tx);
    BOOST_CHECK_MESSAGE(!err.has_value(), "expected consistent, got: " << err.value_or(""));
}
}  // namespace

BOOST_AUTO_TEST_SUITE(OpEthEnvelopeCheckSuite)

// ---- consistent baseline per type ----

BOOST_AUTO_TEST_CASE(LegacyConsistentMirrorPasses)
{
    FakeTx tx;
    tx.m_kind = 0;
    tx.m_extraBytes = legacyEnvelope(7, tx.m_gasLimit, kToHex, tx.m_value, {});
    expectConsistent(tx);
}

BOOST_AUTO_TEST_CASE(AccessListConsistentMirrorPasses)
{
    FakeTx tx;
    tx.m_kind = 1;
    tx.m_extraBytes = accessListEnvelope(kNodeChainId, 7, tx.m_gasLimit, kToHex, tx.m_value, {});
    expectConsistent(tx);
}

BOOST_AUTO_TEST_CASE(Eip1559ConsistentMirrorPasses)
{
    FakeTx tx;
    tx.m_extraBytes = eip1559Envelope(kNodeChainId, 7, tx.m_gasLimit, kToHex, tx.m_value, {});
    expectConsistent(tx);
}

BOOST_AUTO_TEST_CASE(BlobConsistentMirrorPasses)
{
    FakeTx tx;
    tx.m_kind = 3;
    tx.m_blobHashes = {hashFilledWith(0x05)};
    tx.m_extraBytes =
        blobEnvelope(kNodeChainId, 7, tx.m_gasLimit, kToHex, tx.m_value, {}, tx.m_blobHashes);
    expectConsistent(tx);
}

BOOST_AUTO_TEST_CASE(SetCodeConsistentMirrorPasses)
{
    FakeTx tx;
    tx.m_kind = 4;
    tx.m_authorizationList = {makeAuth(1), makeAuth(2)};
    tx.m_extraBytes = setCodeEnvelope(
        kNodeChainId, 7, tx.m_gasLimit, kToHex, tx.m_value, {}, tx.m_authorizationList);
    expectConsistent(tx);
}

// ---- field binds reject a forged mirror ----

BOOST_AUTO_TEST_CASE(MirrorNonceDivergenceRejected)
{
    FakeTx tx;
    tx.m_extraBytes = eip1559Envelope(kNodeChainId, 7, tx.m_gasLimit, kToHex, tx.m_value, {});
    tx.fillFeeDefaults();
    tx.m_nonce = "0x8";
    auto const err = opEthEnvelopeExecutionFieldsMismatch(tx);
    BOOST_REQUIRE(err.has_value());
    BOOST_CHECK_EQUAL(*err, "nonce mismatch");
}

BOOST_AUTO_TEST_CASE(MirrorGasLimitDivergenceRejected)
{
    FakeTx tx;
    tx.m_extraBytes = eip1559Envelope(kNodeChainId, 7, tx.m_gasLimit, kToHex, tx.m_value, {});
    tx.fillFeeDefaults();
    tx.m_gasLimit += 1;
    auto const err = opEthEnvelopeExecutionFieldsMismatch(tx);
    BOOST_REQUIRE(err.has_value());
    BOOST_CHECK_EQUAL(*err, "gasLimit mismatch");
}

BOOST_AUTO_TEST_CASE(MirrorValueDivergenceRejected)
{
    FakeTx tx;
    tx.m_extraBytes = eip1559Envelope(kNodeChainId, 7, tx.m_gasLimit, kToHex, tx.m_value, {});
    tx.fillFeeDefaults();
    tx.m_value += 1;
    auto const err = opEthEnvelopeExecutionFieldsMismatch(tx);
    BOOST_REQUIRE(err.has_value());
    BOOST_CHECK_EQUAL(*err, "value mismatch");

    // Same bind on the legacy shape.
    FakeTx legacy;
    legacy.m_kind = 0;
    legacy.m_extraBytes =
        legacyEnvelope(7, legacy.m_gasLimit, kToHex, legacy.m_value, {});
    legacy.fillFeeDefaults();
    legacy.m_value += 1;
    BOOST_REQUIRE(opEthEnvelopeExecutionFieldsMismatch(legacy).has_value());
}

BOOST_AUTO_TEST_CASE(MirrorToDivergenceRejected)
{
    FakeTx tx;
    tx.m_extraBytes = eip1559Envelope(kNodeChainId, 7, tx.m_gasLimit, kToHex, tx.m_value, {});
    tx.fillFeeDefaults();
    tx.m_to = "0x1111111111111111111111111111111111111111";
    auto const err = opEthEnvelopeExecutionFieldsMismatch(tx);
    BOOST_REQUIRE(err.has_value());
    BOOST_CHECK_EQUAL(*err, "to mismatch");
}

BOOST_AUTO_TEST_CASE(MirrorDataDivergenceRejected)
{
    FakeTx tx;
    tx.m_extraBytes = eip1559Envelope(kNodeChainId, 7, tx.m_gasLimit, kToHex, tx.m_value, {});
    tx.fillFeeDefaults();
    tx.m_input = {0xde, 0xad};
    auto const err = opEthEnvelopeExecutionFieldsMismatch(tx);
    BOOST_REQUIRE(err.has_value());
    BOOST_CHECK_EQUAL(*err, "data mismatch");
}

BOOST_AUTO_TEST_CASE(MirrorFeeFieldsDivergenceRejected)
{
    // legacy gasPrice
    {
        FakeTx tx;
        tx.m_kind = 0;
        tx.m_extraBytes = legacyEnvelope(7, tx.m_gasLimit, kToHex, tx.m_value, {});
        tx.fillFeeDefaults();
        tx.m_gasPrice = kLegacyGasPrice + 1;
        auto const err = opEthEnvelopeExecutionFieldsMismatch(tx);
        BOOST_REQUIRE(err.has_value());
        BOOST_CHECK_EQUAL(*err, "gasPrice mismatch");
    }
    // 0x01 gasPrice
    {
        FakeTx tx;
        tx.m_kind = 1;
        tx.m_extraBytes =
            accessListEnvelope(kNodeChainId, 7, tx.m_gasLimit, kToHex, tx.m_value, {});
        tx.fillFeeDefaults();
        tx.m_gasPrice = kLegacyGasPrice + 1;
        auto const err = opEthEnvelopeExecutionFieldsMismatch(tx);
        BOOST_REQUIRE(err.has_value());
        BOOST_CHECK_EQUAL(*err, "gasPrice mismatch");
    }
    // 0x02 maxFeePerGas / maxPriorityFeePerGas
    {
        FakeTx tx;
        tx.m_extraBytes = eip1559Envelope(kNodeChainId, 7, tx.m_gasLimit, kToHex, tx.m_value, {});
        tx.fillFeeDefaults();
        tx.m_maxFeePerGas = k1559Fee + 1;
        auto const err = opEthEnvelopeExecutionFieldsMismatch(tx);
        BOOST_REQUIRE(err.has_value());
        BOOST_CHECK_EQUAL(*err, "maxFeePerGas mismatch");
    }
    {
        FakeTx tx;
        tx.m_extraBytes = eip1559Envelope(kNodeChainId, 7, tx.m_gasLimit, kToHex, tx.m_value, {});
        tx.fillFeeDefaults();
        tx.m_maxPriorityFeePerGas = k1559Fee + 1;
        auto const err = opEthEnvelopeExecutionFieldsMismatch(tx);
        BOOST_REQUIRE(err.has_value());
        BOOST_CHECK_EQUAL(*err, "maxPriorityFeePerGas mismatch");
    }
    // 0x03 maxFeePerBlobGas
    {
        FakeTx tx;
        tx.m_kind = 3;
        tx.m_extraBytes = blobEnvelope(kNodeChainId, 7, tx.m_gasLimit, kToHex, tx.m_value, {}, {});
        tx.fillFeeDefaults();
        tx.m_maxFeePerBlobGas = 2;
        auto const err = opEthEnvelopeExecutionFieldsMismatch(tx);
        BOOST_REQUIRE(err.has_value());
        BOOST_CHECK_EQUAL(*err, "maxFeePerBlobGas mismatch");
    }
}

BOOST_AUTO_TEST_CASE(MirrorKindDivergenceRejected)
{
    FakeTx tx;
    tx.m_kind = 1;  // mirror claims access_list; the envelope is 0x02
    tx.m_extraBytes = eip1559Envelope(kNodeChainId, 7, tx.m_gasLimit, kToHex, tx.m_value, {});
    tx.fillFeeDefaults();
    auto const err = opEthEnvelopeExecutionFieldsMismatch(tx);
    BOOST_REQUIRE(err.has_value());
    BOOST_CHECK_EQUAL(*err, "tx type mismatch (envelope vs mirror)");
}

BOOST_AUTO_TEST_CASE(EmptyEnvelopeRejected)
{
    FakeTx tx;
    tx.m_extraBytes = {};
    auto const err = opEthEnvelopeExecutionFieldsMismatch(tx);
    BOOST_REQUIRE(err.has_value());
    BOOST_CHECK_EQUAL(*err, "empty extraTransactionBytes");
}

BOOST_AUTO_TEST_CASE(NonCanonicalEnvelopeIntegerRejected)
{
    // 0x00 as a bare byte is not a canonical integer zero (must be 0x80).
    FakeTx tx;
    bcos::bytes payload;
    auto append = [&payload](bcos::bytes const& b) {
        payload.insert(payload.end(), b.begin(), b.end());
    };
    append(rlpInt(kNodeChainId));
    payload.push_back(0x00);  // nonce, non-canonical
    append(rlpInt(k1559Fee));
    append(rlpInt(k1559Fee));
    append(rlpInt(static_cast<uint64_t>(tx.m_gasLimit)));
    append(rlpString(bcos::fromHex(std::string(kToHex.substr(2)))));
    append(rlpInt(static_cast<uint64_t>(tx.m_value)));
    append(rlpString({}));
    payload.push_back(0xc0);
    tx.m_extraBytes = rlpWrapList(0x02, payload, true);
    tx.fillFeeDefaults();
    auto const err = opEthEnvelopeExecutionFieldsMismatch(tx);
    BOOST_REQUIRE(err.has_value());
    BOOST_CHECK_EQUAL(*err, "nonce is not a canonical integer");
}

BOOST_AUTO_TEST_CASE(ContractCreationEnvelopePasses)
{
    FakeTx tx;
    tx.m_to = {};
    tx.m_extraBytes = eip1559Envelope(kNodeChainId, 7, tx.m_gasLimit, "", tx.m_value, {});
    expectConsistent(tx);

    // ...but a creation envelope with a non-empty mirror `to` is a divergence.
    FakeTx forged;
    forged.m_extraBytes = eip1559Envelope(kNodeChainId, 7, forged.m_gasLimit, "", forged.m_value, {});
    forged.fillFeeDefaults();
    auto const err = opEthEnvelopeExecutionFieldsMismatch(forged);
    BOOST_REQUIRE(err.has_value());
    BOOST_CHECK_EQUAL(*err, "to mismatch");
}

// ---- list binds (both directions fail closed) ----

BOOST_AUTO_TEST_CASE(EnvelopeAccessListFullBindPassesAndMismatchRejected)
{
    bcos::protocol::Web3AccessList accessList;
    bcos::protocol::Web3AccessListEntry entry;
    entry.account = addressFilledWith(0x42);
    entry.storageKeys = {hashFilledWith(0x07)};
    accessList.push_back(entry);

    // Envelope carries the list, mirror echoes it: pass.
    FakeTx tx;
    tx.m_accessList = accessList;
    bcos::bytes payload;
    auto append = [&payload](bcos::bytes const& b) {
        payload.insert(payload.end(), b.begin(), b.end());
    };
    append(rlpInt(kNodeChainId));
    append(rlpInt(7));
    append(rlpInt(k1559Fee));
    append(rlpInt(k1559Fee));
    append(rlpInt(static_cast<uint64_t>(tx.m_gasLimit)));
    append(rlpString(bcos::fromHex(std::string(kToHex.substr(2)))));
    append(rlpInt(static_cast<uint64_t>(tx.m_value)));
    append(rlpString({}));
    append(rlpAccessList(accessList));
    tx.m_extraBytes = rlpWrapList(0x02, payload, true);
    expectConsistent(tx);

    // Mirror stripped against a non-empty envelope list: reject.
    FakeTx stripped;
    stripped.m_extraBytes = tx.m_extraBytes;
    stripped.fillFeeDefaults();
    BOOST_REQUIRE(opEthEnvelopeExecutionFieldsMismatch(stripped).has_value());

    // Mirror content differs: reject.
    FakeTx forged;
    forged.m_extraBytes = tx.m_extraBytes;
    forged.fillFeeDefaults();
    forged.m_accessList = accessList;
    forged.m_accessList[0].storageKeys = {hashFilledWith(0x08)};
    BOOST_REQUIRE(opEthEnvelopeExecutionFieldsMismatch(forged).has_value());
}

BOOST_AUTO_TEST_CASE(NonEmptyUnboundListsAreFailClosed)
{
    // Legacy envelope has no accessList field: a non-empty mirror list is unbound.
    {
        FakeTx tx;
        tx.m_kind = 0;
        tx.m_extraBytes = legacyEnvelope(7, tx.m_gasLimit, kToHex, tx.m_value, {});
        tx.fillFeeDefaults();
        bcos::protocol::Web3AccessListEntry entry;
        entry.account = addressFilledWith(0x42);
        tx.m_accessList = {entry};
        auto const err = opEthEnvelopeExecutionFieldsMismatch(tx);
        BOOST_REQUIRE(err.has_value());
        BOOST_CHECK_EQUAL(*err, "accessList is not bound to the signed envelope");
    }
    // 0x01 has no blob fields: a non-empty mirror blobHashes is unbound.
    {
        FakeTx tx;
        tx.m_kind = 1;
        tx.m_extraBytes =
            accessListEnvelope(kNodeChainId, 7, tx.m_gasLimit, kToHex, tx.m_value, {});
        tx.fillFeeDefaults();
        tx.m_blobHashes = {hashFilledWith(0x01)};
        auto const err = opEthEnvelopeExecutionFieldsMismatch(tx);
        BOOST_REQUIRE(err.has_value());
        BOOST_CHECK_EQUAL(*err, "blobVersionedHashes is not bound to the signed envelope");
    }
    // 0x02 without the 4844 extension carrying a mirror blobHashes: unbound.
    {
        FakeTx tx;
        tx.m_extraBytes = eip1559Envelope(kNodeChainId, 7, tx.m_gasLimit, kToHex, tx.m_value, {});
        tx.fillFeeDefaults();
        tx.m_blobHashes = {hashFilledWith(0x01)};
        auto const err = opEthEnvelopeExecutionFieldsMismatch(tx);
        BOOST_REQUIRE(err.has_value());
        BOOST_CHECK_EQUAL(*err, "blobVersionedHashes is not bound to the signed envelope");
    }
}

BOOST_AUTO_TEST_CASE(BlobHashesContentMismatchRejected)
{
    FakeTx tx;
    tx.m_kind = 3;
    tx.m_blobHashes = {hashFilledWith(0x05)};
    tx.m_extraBytes = blobEnvelope(
        kNodeChainId, 7, tx.m_gasLimit, kToHex, tx.m_value, {}, {hashFilledWith(0x06)});
    tx.fillFeeDefaults();
    auto const err = opEthEnvelopeExecutionFieldsMismatch(tx);
    BOOST_REQUIRE(err.has_value());
    BOOST_CHECK_EQUAL(*err, "blobVersionedHashes is not bound to the signed envelope");
}

// ---- authorizationList (EIP-7702) ----

BOOST_AUTO_TEST_CASE(AuthorizationListContentMismatchRejected)
{
    FakeTx tx;
    tx.m_kind = 4;
    tx.m_authorizationList = {makeAuth(1)};
    tx.m_extraBytes = setCodeEnvelope(
        kNodeChainId, 7, tx.m_gasLimit, kToHex, tx.m_value, {}, {makeAuth(9)});
    tx.fillFeeDefaults();
    auto const err = opEthEnvelopeExecutionFieldsMismatch(tx);
    BOOST_REQUIRE(err.has_value());
    BOOST_CHECK_EQUAL(*err, "authorizationList is not bound to the signed envelope");
}

BOOST_AUTO_TEST_CASE(BlockPathRejectsUnboundAuthorizationList)
{
    // A non-empty mirror authorizationList on a non-0x04 tx is unbound — both
    // the field walker and the dedicated block-path gate must reject it.
    FakeTx tx;
    tx.m_extraBytes = eip1559Envelope(kNodeChainId, 7, tx.m_gasLimit, kToHex, tx.m_value, {});
    tx.fillFeeDefaults();
    tx.m_authorizationList = {makeAuth(1)};

    auto const fieldErr = opEthEnvelopeExecutionFieldsMismatch(tx);
    BOOST_REQUIRE(fieldErr.has_value());
    BOOST_CHECK_EQUAL(*fieldErr, "authorizationList is not bound to the signed envelope");

    auto const gateErr = opEthBlockPathUnboundAuthorizationList(tx);
    BOOST_REQUIRE(gateErr.has_value());
    BOOST_CHECK_EQUAL(*gateErr, "authorizationList is not bound to the signed envelope");
}

BOOST_AUTO_TEST_CASE(BlockPathSetCodeEmptyMirrorListPassesGate)
{
    // set_code with an empty mirror list is opValidate's problem, not this gate's.
    FakeTx tx;
    tx.m_kind = 4;
    BOOST_CHECK(!opEthBlockPathUnboundAuthorizationList(tx).has_value());
    // A bound non-empty list passes the gate.
    tx.m_authorizationList = {makeAuth(1)};
    BOOST_CHECK(!opEthBlockPathUnboundAuthorizationList(tx).has_value());
}

// ---- zero sender ----

BOOST_AUTO_TEST_CASE(BlockPathRejectsEmptySender)
{
    FakeTx tx;
    tx.m_sender.clear();
    auto const err = opEthBlockPathZeroSender(tx);
    BOOST_REQUIRE(err.has_value());
    BOOST_CHECK_EQUAL(*err, "empty sender");

    FakeTx ok;
    BOOST_CHECK(!opEthBlockPathZeroSender(ok).has_value());

    // A full-width all-zero sender is still address(0) — rejected like the empty one.
    FakeTx zero;
    zero.m_sender.assign(sizeof(evmc_address), '\0');
    BOOST_REQUIRE(opEthBlockPathZeroSender(zero).has_value());
}

// ---- chainId gate ----

BOOST_AUTO_TEST_CASE(EnvelopeChainIdWinsOverMirror)
{
    FakeTx tx;
    tx.m_extraBytes = eip1559Envelope(9, 7, tx.m_gasLimit, kToHex, tx.m_value, {});
    tx.m_chainId = "10";  // mirror claims the node chainId
    auto const gate = opEthEnvelopeChainIdMismatch(tx, kNodeChainId);
    BOOST_REQUIRE(gate.has_value());
    BOOST_CHECK_EQUAL(*gate, "tx envelope chain_id 9 does not match node chainId 10");
}

BOOST_AUTO_TEST_CASE(EnvelopeChainIdMatchesNode)
{
    FakeTx tx;
    tx.m_extraBytes = eip1559Envelope(kNodeChainId, 7, tx.m_gasLimit, kToHex, tx.m_value, {});
    tx.m_chainId = "999";  // mirror is wrong but irrelevant
    BOOST_CHECK(!opEthEnvelopeChainIdMismatch(tx, kNodeChainId).has_value());
}

BOOST_AUTO_TEST_CASE(TypedEnvelopeWithoutParseableChainIdRejected)
{
    // A type byte followed by a malformed list is not a legacy/pre-EIP-155
    // exemption and must fail closed.
    bcos::bytes typedEmptyList{0x02, 0xc0};
    auto const gate = opEthEnvelopeChainIdMismatch(
        bcos::bytesConstRef{typedEmptyList.data(), typedEmptyList.size()}, kNodeChainId);
    BOOST_REQUIRE(gate.has_value());
    BOOST_CHECK_EQUAL(*gate, "typed tx envelope is missing a parseable chainId");
}

BOOST_AUTO_TEST_CASE(LegacyChainIdForms)
{
    // Unprotected v=27 passes at any node chainId.
    {
        FakeTx tx;
        tx.m_kind = 0;
        tx.m_extraBytes = legacyEnvelope(7, tx.m_gasLimit, kToHex, tx.m_value, {});
        BOOST_CHECK(!opEthEnvelopeChainIdMismatch(tx, kNodeChainId).has_value());
    }
    // Protected v=37 → chainId 1: matches node 1, rejects node 10.
    {
        bcos::bytes payload;
        auto append = [&payload](bcos::bytes const& b) {
            payload.insert(payload.end(), b.begin(), b.end());
        };
        append(rlpInt(7));
        append(rlpInt(kLegacyGasPrice));
        append(rlpInt(5000000));
        append(rlpString(bcos::fromHex(std::string(kToHex.substr(2)))));
        append(rlpInt(5));
        append(rlpString({}));
        append(rlpInt(37));  // v → chainId (37-35)>>1 = 1
        append(rlpInt(1));
        append(rlpInt(2));
        auto env = rlpWrapList(0, payload, false);
        BOOST_CHECK(!opEthEnvelopeChainIdMismatch(
                        bcos::bytesConstRef{env.data(), env.size()}, 1)
                         .has_value());
        BOOST_CHECK(opEthEnvelopeChainIdMismatch(bcos::bytesConstRef{env.data(), env.size()}, 10)
                        .has_value());
    }
    // Malformed v (0/1, 29-34) fails closed, never folded into the unprotected exemption.
    for (uint64_t badV : {0u, 1u, 29u, 30u, 34u})
    {
        bcos::bytes payload;
        auto append = [&payload](bcos::bytes const& b) {
            payload.insert(payload.end(), b.begin(), b.end());
        };
        append(rlpInt(7));
        append(rlpInt(kLegacyGasPrice));
        append(rlpInt(5000000));
        append(rlpString(bcos::fromHex(std::string(kToHex.substr(2)))));
        append(rlpInt(5));
        append(rlpString({}));
        append(rlpInt(badV));
        append(rlpInt(1));
        append(rlpInt(2));
        auto env = rlpWrapList(0, payload, false);
        auto const gate =
            opEthEnvelopeChainIdMismatch(bcos::bytesConstRef{env.data(), env.size()}, kNodeChainId);
        BOOST_CHECK_MESSAGE(gate.has_value(), "malformed v=" << badV << " must fail closed");
        if (gate.has_value())
            BOOST_CHECK(std::string(*gate).find("malformed chainId/v") != std::string::npos);
    }
}

BOOST_AUTO_TEST_CASE(DepositEnvelopeSkipsChainIdGate)
{
    // 0x7E envelopes carry no chainId (field 0 is sourceHash): the gate skips them.
    bcos::executor_v1::opstack::DepositTx dep{};
    std::fill(std::begin(dep.sourceHash.bytes), std::end(dep.sourceHash.bytes), 0x11);
    std::fill(std::begin(dep.from.bytes), std::end(dep.from.bytes), 0x22);
    dep.to = std::nullopt;
    dep.value = 0;
    dep.gasLimit = 100000;
    auto const env = bcos::executor_v1::opstack::encodeOpEthDepositEnvelope(dep);
    BOOST_REQUIRE(!env.empty());
    BOOST_CHECK_EQUAL(env.front(), 0x7e);
    BOOST_CHECK(!opEthEnvelopeChainIdMismatch(
                    bcos::bytesConstRef{env.data(), env.size()}, kNodeChainId)
                     .has_value());
}

// The remaining unprotected legacy forms stay exempt: the 6-field EIP-155 preimage
// (no v/r/s at all) and a full envelope with v=28 (LegacyChainIdForms pins v=27).
BOOST_AUTO_TEST_CASE(LegacyUnprotectedPreimageAndV28PassChainIdGate)
{
    // 6-field preimage → unprotected exemption.
    {
        bcos::bytes payload;
        auto append = [&payload](bcos::bytes const& b) {
            payload.insert(payload.end(), b.begin(), b.end());
        };
        append(rlpInt(7));
        append(rlpInt(kLegacyGasPrice));
        append(rlpInt(5000000));
        append(rlpString(bcos::fromHex(std::string(kToHex.substr(2)))));
        append(rlpInt(5));
        append(rlpString({}));
        auto const env = rlpWrapList(0, payload, false);
        BOOST_CHECK(!opEthEnvelopeChainIdMismatch(
                        bcos::bytesConstRef{env.data(), env.size()}, kNodeChainId)
                         .has_value());
    }
    // Full envelope with v=28 → unprotected exemption.
    {
        bcos::bytes payload;
        auto append = [&payload](bcos::bytes const& b) {
            payload.insert(payload.end(), b.begin(), b.end());
        };
        append(rlpInt(7));
        append(rlpInt(kLegacyGasPrice));
        append(rlpInt(5000000));
        append(rlpString(bcos::fromHex(std::string(kToHex.substr(2)))));
        append(rlpInt(5));
        append(rlpString({}));
        append(rlpInt(28));  // v
        append(rlpInt(1));   // r
        append(rlpInt(2));   // s
        auto const env = rlpWrapList(0, payload, false);
        BOOST_CHECK(!opEthEnvelopeChainIdMismatch(
                        bcos::bytesConstRef{env.data(), env.size()}, kNodeChainId)
                         .has_value());
    }
}

// The convenience overload must derive chainId from the envelope BYTES: a mirror whose
// web3ChainIdFromEnvelope override agrees with the node must still be rejected when the
// signed envelope's chainId differs.
BOOST_AUTO_TEST_CASE(EnvelopeChainIdGateIgnoresDivergentVirtualOverride)
{
    DivergentChainIdTx tx;  // override reports kNodeChainId...
    tx.m_extraBytes = eip1559Envelope(9, 7, tx.m_gasLimit, kToHex, tx.m_value, {});
    auto const gate = opEthEnvelopeChainIdMismatch(tx, kNodeChainId);
    BOOST_REQUIRE(gate.has_value());
    BOOST_CHECK_EQUAL(*gate, "tx envelope chain_id 9 does not match node chainId 10");
}

// The 4844-in-1559 extension carries maxFeePerBlobGas at idx 9 too: a consistent mirror
// passes, a forged one is rejected like the 0x03 arm, or the envelope's signed blob-fee
// cap is never compared on a 14-item 0x02 tx.
BOOST_AUTO_TEST_CASE(Eip1559BlobExtensionFeeBind)
{
    FakeTx tx;
    tx.m_extraBytes =
        eip1559BlobExtEnvelope(kNodeChainId, 7, tx.m_gasLimit, kToHex, tx.m_value, {});
    tx.fillFeeDefaults();  // kind 2: maxFeePerBlobGas untouched by fillFeeDefaults
    tx.m_maxFeePerBlobGas = 1;
    BOOST_CHECK(!opEthEnvelopeExecutionFieldsMismatch(tx).has_value());

    tx.m_maxFeePerBlobGas = 2;  // envelope says 1
    auto const err = opEthEnvelopeExecutionFieldsMismatch(tx);
    BOOST_REQUIRE(err.has_value());
    BOOST_CHECK_EQUAL(*err, "maxFeePerBlobGas mismatch");
}

// The field-index table's 0x01 (2930) value arm (idx 5): a forged mirror value is rejected.
BOOST_AUTO_TEST_CASE(AccessListEnvelopeValueDivergenceRejected)
{
    FakeTx tx;
    tx.m_kind = 1;
    tx.m_extraBytes = accessListEnvelope(kNodeChainId, 7, tx.m_gasLimit, kToHex, tx.m_value, {});
    tx.fillFeeDefaults();
    tx.m_value += 1;  // forged
    auto const err = opEthEnvelopeExecutionFieldsMismatch(tx);
    BOOST_REQUIRE(err.has_value());
    BOOST_CHECK_EQUAL(*err, "value mismatch");
}

// The 0x03 value arm: the shared typed index layout (value at idx 6) must bind on blob txs.
BOOST_AUTO_TEST_CASE(BlobEnvelopeValueDivergenceRejected)
{
    FakeTx tx;
    tx.m_kind = 3;
    tx.m_extraBytes = blobEnvelope(kNodeChainId, 7, tx.m_gasLimit, kToHex, tx.m_value, {}, {});
    tx.fillFeeDefaults();
    tx.m_value += 1;  // forged
    auto const err = opEthEnvelopeExecutionFieldsMismatch(tx);
    BOOST_REQUIRE(err.has_value());
    BOOST_CHECK_EQUAL(*err, "value mismatch");
}

// ContractCreationEnvelopePasses covers mirror-to against a creation envelope; this is the
// other direction — the envelope carries a recipient but the mirror claims creation.
BOOST_AUTO_TEST_CASE(EnvelopeToPresentMirrorCreationRejected)
{
    FakeTx tx;
    tx.m_to = {};  // creation mirror
    tx.m_extraBytes = eip1559Envelope(kNodeChainId, 7, tx.m_gasLimit, kToHex, tx.m_value, {});
    tx.fillFeeDefaults();
    auto const err = opEthEnvelopeExecutionFieldsMismatch(tx);
    BOOST_REQUIRE(err.has_value());
    BOOST_CHECK_EQUAL(*err, "to mismatch");
}

// Ethereum transaction to/data are byte strings, never RLP lists: an empty-list payload
// must not pass the gate.
BOOST_AUTO_TEST_CASE(ListShapedToAndDataAreRejected)
{
    FakeTx tx;
    tx.m_to = {};
    tx.m_extraBytes =
        eip1559Envelope(kNodeChainId, 7, tx.m_gasLimit, "", tx.m_value, {}, /*toIsList=*/true);
    tx.fillFeeDefaults();
    auto err = opEthEnvelopeExecutionFieldsMismatch(tx);
    BOOST_REQUIRE(err.has_value());
    BOOST_CHECK_EQUAL(*err, "to field is an RLP list");

    FakeTx tx2;
    tx2.m_to = {};
    tx2.m_extraBytes = eip1559Envelope(kNodeChainId, 7, tx2.m_gasLimit, "", tx2.m_value, {},
        /*toIsList=*/false, /*dataIsList=*/true);
    tx2.fillFeeDefaults();
    err = opEthEnvelopeExecutionFieldsMismatch(tx2);
    BOOST_REQUIRE(err.has_value());
    BOOST_CHECK_EQUAL(*err, "data field is an RLP list");
}

// nonce/gasLimit/value are scalars, never RLP lists — on every field-index shape
// (legacy [0,2,4], 0x01 [1,3,5], 0x02 [1,4,6]).
BOOST_AUTO_TEST_CASE(ListShapedIntegerFieldsAreRejected)
{
    auto run1559 = [](bool nonceIsList, bool gasIsList, bool valueIsList, char const* needle) {
        FakeTx tx;
        tx.m_extraBytes = eip1559Envelope(kNodeChainId, 7, tx.m_gasLimit, kToHex, tx.m_value, {},
            false, false, nonceIsList, gasIsList, valueIsList);
        tx.fillFeeDefaults();
        auto const err = opEthEnvelopeExecutionFieldsMismatch(tx);
        BOOST_REQUIRE(err.has_value());
        BOOST_CHECK_EQUAL(*err, needle);
    };
    run1559(true, false, false, "nonce field is an RLP list");
    run1559(false, true, false, "gasLimit field is an RLP list");
    run1559(false, false, true, "value field is an RLP list");

    auto runLegacy = [](bool nonceIsList, bool gasIsList, bool valueIsList, char const* needle) {
        FakeTx tx;
        tx.m_kind = 0;
        tx.m_extraBytes = legacyEnvelope(
            7, tx.m_gasLimit, kToHex, tx.m_value, {}, nonceIsList, gasIsList, valueIsList);
        tx.fillFeeDefaults();
        auto const err = opEthEnvelopeExecutionFieldsMismatch(tx);
        BOOST_REQUIRE(err.has_value());
        BOOST_CHECK_EQUAL(*err, needle);
    };
    runLegacy(true, false, false, "nonce field is an RLP list");
    runLegacy(false, true, false, "gasLimit field is an RLP list");
    runLegacy(false, false, true, "value field is an RLP list");

    auto runAccessList = [](bool nonceIsList, bool gasIsList, bool valueIsList,
                             char const* needle) {
        FakeTx tx;
        tx.m_kind = 1;
        tx.m_extraBytes = accessListEnvelope(kNodeChainId, 7, tx.m_gasLimit, kToHex, tx.m_value,
            {}, nonceIsList, gasIsList, valueIsList);
        tx.fillFeeDefaults();
        auto const err = opEthEnvelopeExecutionFieldsMismatch(tx);
        BOOST_REQUIRE(err.has_value());
        BOOST_CHECK_EQUAL(*err, needle);
    };
    runAccessList(true, false, false, "nonce field is an RLP list");
    runAccessList(false, true, false, "gasLimit field is an RLP list");
    runAccessList(false, false, true, "value field is an RLP list");
}

// The blob bind in both count directions: a mirror stripped against the envelope's hashes
// is rejected, and a non-empty mirror against the envelope's empty list is rejected.
BOOST_AUTO_TEST_CASE(EnvelopeBlobHashesCountMismatchRejected)
{
    std::vector<bcos::h256> const hashes = {hashFilledWith(0x05), hashFilledWith(0x06)};

    // Stripped mirror: the envelope's hashes are gone from the mirror.
    FakeTx stripped;
    stripped.m_kind = 3;
    stripped.m_extraBytes =
        blobEnvelope(kNodeChainId, 7, stripped.m_gasLimit, kToHex, stripped.m_value, {}, hashes);
    stripped.fillFeeDefaults();
    auto const strippedErr = opEthEnvelopeExecutionFieldsMismatch(stripped);
    BOOST_REQUIRE(strippedErr.has_value());
    BOOST_CHECK_EQUAL(*strippedErr, "blobVersionedHashes is not bound to the signed envelope");

    // Inflated mirror: the envelope's list is empty but the mirror carries a hash.
    FakeTx inflated;
    inflated.m_kind = 3;
    inflated.m_extraBytes =
        blobEnvelope(kNodeChainId, 7, inflated.m_gasLimit, kToHex, inflated.m_value, {}, {});
    inflated.fillFeeDefaults();
    inflated.m_blobHashes = {hashFilledWith(0x07)};
    auto const inflatedErr = opEthEnvelopeExecutionFieldsMismatch(inflated);
    BOOST_REQUIRE(inflatedErr.has_value());
    BOOST_CHECK_EQUAL(*inflatedErr, "blobVersionedHashes is not bound to the signed envelope");
}

// A type-0x03 envelope that stops after maxFeePerBlobGas (10 items, no blobVersionedHashes
// at idx 10) is malformed. The fewer-fields guard must reject it BEFORE the blob bind
// dereferences blobPayload: blobFieldsPresent is true for every 0x03 envelope, but
// blobPayload is engaged only when the walk reached idx 10.
BOOST_AUTO_TEST_CASE(ShortType03EnvelopeFewerFieldsRejected)
{
    bcos::bytes payload;
    auto append = [&payload](bcos::bytes const& b) {
        payload.insert(payload.end(), b.begin(), b.end());
    };
    append(rlpInt(kNodeChainId));
    append(rlpInt(7));
    append(rlpInt(k1559Fee));
    append(rlpInt(k1559Fee));
    append(rlpInt(5000000));
    append(rlpString(bcos::fromHex(std::string(kToHex.substr(2)))));
    append(rlpInt(5));
    append(rlpString({0xde}));
    payload.push_back(0xc0);  // accessList (idx 8)
    append(rlpInt(1));        // maxFeePerBlobGas (idx 9) — no idx 10 follows

    FakeTx tx;
    tx.m_kind = 3;
    tx.m_extraBytes = rlpWrapList(0x03, payload, true);
    tx.fillFeeDefaults();
    tx.m_blobHashes = {hashFilledWith(0x05)};  // non-empty mirror: the old code dereferenced
                                               // the disengaged blobPayload right here
    auto const err = opEthEnvelopeExecutionFieldsMismatch(tx);
    BOOST_REQUIRE(err.has_value());
    BOOST_CHECK_EQUAL(*err, "envelope has fewer fields than the type requires");
}

// The bind's fixed-width memcpys must fit the remaining buffer: an accessList address item
// that declares 20 bytes but carries fewer is "malformed", never an out-of-bounds read
// (decodeHeader does not bound payloadLength against the view; the walk re-checks, these
// sub-decodes must too).
BOOST_AUTO_TEST_CASE(TruncatedAccessListAddressRejected)
{
    // Address item: string header declaring 20, only 5 bytes present.
    bcos::bytes addrItem{0x94, 0x01, 0x02, 0x03, 0x04, 0x05};
    bcos::bytes entryPayload(addrItem);
    entryPayload.push_back(0xc0);  // empty storageKeys
    bcos::bytes entry;
    rlp::encodeHeader(entry, {.isList = true, .payloadLength = entryPayload.size()});
    entry.insert(entry.end(), entryPayload.begin(), entryPayload.end());
    bcos::bytes accessListItem;
    rlp::encodeHeader(accessListItem, {.isList = true, .payloadLength = entry.size()});
    accessListItem.insert(accessListItem.end(), entry.begin(), entry.end());

    bcos::bytes payload;
    auto append = [&payload](bcos::bytes const& b) {
        payload.insert(payload.end(), b.begin(), b.end());
    };
    append(rlpInt(kNodeChainId));
    append(rlpInt(7));
    append(rlpInt(kLegacyGasPrice));
    append(rlpInt(5000000));
    append(rlpString(bcos::fromHex(std::string(kToHex.substr(2)))));
    append(rlpInt(5));
    append(rlpString({0xde}));
    append(accessListItem);  // accessList (idx 7 for 0x01)

    FakeTx tx;
    tx.m_kind = 1;
    tx.m_extraBytes = rlpWrapList(0x01, payload, true);
    tx.fillFeeDefaults();
    tx.m_input = {0xde};
    bcos::protocol::Web3AccessListEntry entry2;
    entry2.account = addressFilledWith(0x42);
    tx.m_accessList = {entry2};
    auto const err = opEthEnvelopeExecutionFieldsMismatch(tx);
    BOOST_REQUIRE(err.has_value());
    BOOST_CHECK_EQUAL(*err, "accessList address is malformed");
}

// Same bound check for the blob field: a 32-declaring hash item with fewer bytes present is
// "malformed", not an out-of-bounds read of the truncated tail.
BOOST_AUTO_TEST_CASE(TruncatedBlobHashRejected)
{
    // Blob hash item: string header declaring 32, only 5 bytes present.
    bcos::bytes hashItem{0xa0, 0x01, 0x02, 0x03, 0x04, 0x05};
    bcos::bytes blobListItem;
    rlp::encodeHeader(blobListItem, {.isList = true, .payloadLength = hashItem.size()});
    blobListItem.insert(blobListItem.end(), hashItem.begin(), hashItem.end());

    bcos::bytes payload;
    auto append = [&payload](bcos::bytes const& b) {
        payload.insert(payload.end(), b.begin(), b.end());
    };
    append(rlpInt(kNodeChainId));
    append(rlpInt(7));
    append(rlpInt(k1559Fee));
    append(rlpInt(k1559Fee));
    append(rlpInt(5000000));
    append(rlpString(bcos::fromHex(std::string(kToHex.substr(2)))));
    append(rlpInt(5));
    append(rlpString({0xde}));
    payload.push_back(0xc0);  // accessList
    append(rlpInt(1));        // maxFeePerBlobGas
    append(blobListItem);     // blobVersionedHashes (idx 10)

    FakeTx tx;
    tx.m_kind = 3;
    tx.m_extraBytes = rlpWrapList(0x03, payload, true);
    tx.fillFeeDefaults();
    tx.m_input = {0xde};
    tx.m_blobHashes = {hashFilledWith(0x05)};
    auto const err = opEthEnvelopeExecutionFieldsMismatch(tx);
    BOOST_REQUIRE(err.has_value());
    BOOST_CHECK_EQUAL(*err, "blobVersionedHashes entry is malformed");
}

BOOST_AUTO_TEST_SUITE_END()
