// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0

// OpTxConvertTest — toEvmoneTransaction (the tars→evmone transaction conversion). Covers the
// kind mapping (incl. fail-closed default), decimal chainId vs hex-quantity nonce, the three
// `to` forms, and fee defaults. Uses a hand-rolled FakeTransaction over the abstract
// protocol::Transaction (30 pure virtuals, mostly unused stubs) to avoid pulling a concrete
// implementation's link chain into this binary.

#include <opstack-executor/OpstackExecutor.h>

#include <bcos-framework/protocol/Transaction.h>
#include <boost/test/unit_test.hpp>
#include <evmc/evmc.hpp>

#include <cstdint>
#include <optional>
#include <string>

using bcos::evm::engine::OpConsensusError;
using bcos::executor_v1::eth::toEvmoneTransaction;

namespace
{
class FakeTransaction : public bcos::protocol::Transaction
{
public:
    // ---- knobs read by toEvmoneTransaction ----
    uint8_t m_kind = 2;
    bcos::bytes m_input;
    int64_t m_gasLimit = 1000000;
    std::optional<bcos::u256> m_gasPrice;
    std::optional<bcos::u256> m_maxFeePerGas;
    std::optional<bcos::u256> m_maxPriorityFeePerGas;
    std::optional<bcos::u256> m_maxFeePerBlobGas;
    std::string m_sender = std::string(sizeof(evmc_address), '\xaa');  // raw 20 bytes
    std::string m_to;  // hex form ("0x" + 40 hex chars), or empty = creation
    bcos::u256 m_value = 0;
    std::string m_chainId;        // DECIMAL string (Web3Transaction.cpp writes std::to_string)
    std::string m_nonce = "0x0";  // hex quantity
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
}  // namespace

BOOST_AUTO_TEST_SUITE(OpTxConvertTest)

BOOST_AUTO_TEST_CASE(LegacyKindDefaultsPriorityFeeToGasPrice)
{
    FakeTransaction tx;
    tx.m_kind = 0;
    tx.m_gasPrice = bcos::u256{5};
    auto evmTx = toEvmoneTransaction(tx);
    BOOST_CHECK(evmTx.type == evmone::state::Transaction::Type::legacy);
    BOOST_CHECK(evmTx.max_gas_price == intx::uint256{5});
    // legacy default: priority fee follows the gas price (coinbase tip under base_fee=0)
    BOOST_CHECK(evmTx.max_priority_gas_price == intx::uint256{5});
    BOOST_CHECK_EQUAL(evmTx.gas_limit, 1000000);
}

BOOST_AUTO_TEST_CASE(Eip1559KindMapsFeeFields)
{
    FakeTransaction tx;
    tx.m_kind = 2;
    tx.m_maxFeePerGas = bcos::u256{7};
    tx.m_maxPriorityFeePerGas = bcos::u256{3};
    auto evmTx = toEvmoneTransaction(tx);
    BOOST_CHECK(evmTx.type == evmone::state::Transaction::Type::eip1559);
    BOOST_CHECK(evmTx.max_gas_price == intx::uint256{7});
    BOOST_CHECK(evmTx.max_priority_gas_price == intx::uint256{3});
}

BOOST_AUTO_TEST_CASE(KindMappingCoversAllKnownTypes)
{
    const std::pair<uint8_t, evmone::state::Transaction::Type> cases[] = {
        {0, evmone::state::Transaction::Type::legacy},
        {1, evmone::state::Transaction::Type::access_list},
        {2, evmone::state::Transaction::Type::eip1559},
        {3, evmone::state::Transaction::Type::blob},
        {4, evmone::state::Transaction::Type::set_code},
    };
    for (auto const& [kind, expected] : cases)
    {
        FakeTransaction tx;
        tx.m_kind = kind;
        BOOST_CHECK_MESSAGE(
            toEvmoneTransaction(tx).type == expected, "kind " << static_cast<int>(kind));
    }
}

BOOST_AUTO_TEST_CASE(UnknownKindFailsClosed)
{
    // An out-of-range tars wire kind must NOT fold into legacy
    // (op-geth UnmarshalBinary: ErrTxTypeNotSupported).
    for (uint8_t kind : {5, 0x7d, 0x7f, 0xff})
    {
        FakeTransaction tx;
        tx.m_kind = kind;
        BOOST_CHECK_THROW(toEvmoneTransaction(tx), OpConsensusError);
    }
}

BOOST_AUTO_TEST_CASE(ChainIdIsDecimalNonceIsHexQuantity)
{
    {
        FakeTransaction tx;
        tx.m_chainId = "10";  // decimal: 10, NOT 0x10 = 16
        tx.m_nonce = "0x1a";  // hex quantity: 26
        auto evmTx = toEvmoneTransaction(tx);
        BOOST_CHECK_EQUAL(evmTx.chain_id, 10u);
        BOOST_CHECK_EQUAL(evmTx.nonce, 26u);
    }
    // malformed / unparseable values fall through to 0 (the signature binds both fields, so a
    // malformed value implies admission has already failed upstream)
    for (auto const* bad : {"", "10x", "chain0", "0x10", "-1", "18446744073709551616"})
    {
        FakeTransaction tx;
        tx.m_chainId = bad;
        BOOST_CHECK_MESSAGE(toEvmoneTransaction(tx).chain_id == 0u, "chainId '" << bad << "'");
    }
    {
        FakeTransaction tx;
        tx.m_nonce = "notHex";
        BOOST_CHECK_EQUAL(toEvmoneTransaction(tx).nonce, 0u);
    }
}

BOOST_AUTO_TEST_CASE(ToFieldThreeForms)
{
    // well-formed hex address
    {
        FakeTransaction tx;
        tx.m_to = "0x1111111111111111111111111111111111111111";
        auto evmTx = toEvmoneTransaction(tx);
        BOOST_REQUIRE(evmTx.to.has_value());
        BOOST_CHECK_EQUAL(evmTx.to->bytes[0], 0x11);
        BOOST_CHECK_EQUAL(evmTx.to->bytes[19], 0x11);
    }
    // empty -> contract creation
    {
        FakeTransaction tx;
        tx.m_to = "";
        BOOST_CHECK(!toEvmoneTransaction(tx).to.has_value());
    }
    // short/malformed hex must NOT be left-aligned into an address — stays creation
    for (auto const* bad : {"0x1234", "0x", "0xzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz",
             "111111111111111111111111111111111111111" /* 39 hex chars */})
    {
        FakeTransaction tx;
        tx.m_to = bad;
        BOOST_CHECK_MESSAGE(!toEvmoneTransaction(tx).to.has_value(), "to '" << bad << "'");
    }
}

BOOST_AUTO_TEST_CASE(ValueSenderAndInputPassThrough)
{
    FakeTransaction tx;
    tx.m_value = bcos::u256{12345};
    tx.m_input = {0xde, 0xad};
    auto evmTx = toEvmoneTransaction(tx);
    BOOST_CHECK(evmTx.value == intx::uint256{12345});
    for (size_t i = 0; i < sizeof(evmc_address); ++i)
        BOOST_CHECK_EQUAL(evmTx.sender.bytes[i], 0xaa);
    BOOST_REQUIRE_EQUAL(evmTx.data.size(), 2u);
    BOOST_CHECK_EQUAL(evmTx.data[0], 0xde);
}

BOOST_AUTO_TEST_SUITE_END()
