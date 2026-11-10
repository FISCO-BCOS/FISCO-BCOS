/// @file TestEthL1ReceiptContractAddress.cpp
/// @brief The eth lane's creation receipt must report the created address.
///
/// eth_getTransactionReceipt answered `contractAddress: null` for every deployment on an
/// executor_version=2 chain because EthL1Policy::buildReceipt handed the receipt factory an
/// empty string unconditionally, while the OP lane had always filled it. This pins the eth lane
/// end of that contract: a top-level CREATE reports CreateAddress(sender, pre-execution nonce)
/// in the FISCO receipt's string form (40 lowercase hex, no "0x" -- the RPC layer adds the prefix
/// and EIP-55 checksums it), a CALL keeps it empty, and the nonce used is the one BEFORE the
/// sender's bump (what evmone's prepare_message derives from), not the post-increment value.
///
/// The address itself comes from evm::compute_create_address, shared with the OP lane through
/// evm::toFiscoContractAddress, so the two lanes cannot format one RPC field differently.
///
/// What this does NOT cover: the `!to.has_value()` wiring inside runTransaction. That needs a
/// signed, validated transaction and a seeded EthereumState; the shared helper it feeds is
/// pinned here and by TestEthereumStateSmoke's Yellow-Paper CREATE vectors.

#include <boost/test/unit_test.hpp>

#include "bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h"
#include "ethereum-executor/EVMSupport.h"
#include "ethereum-executor/EthExecutionPolicy.h"

#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>

#include <array>
#include <string>
#include <vector>

using namespace bcos;
using namespace bcos::executor_v1::eth;
using namespace evmc::literals;

namespace
{
/// The only host member EthL1Policy::buildReceipt touches is take_logs(). A stub keeps this
/// test about the receipt's address field instead of dragging in a storage backend and a VM.
struct StubHost
{
    std::vector<evm::Log> logs;
    std::vector<evm::Log> take_logs() { return std::move(logs); }
};

protocol::TransactionReceiptFactory::Ptr makeReceiptFactory()
{
    auto suite = std::make_shared<crypto::CryptoSuite>(
        std::make_shared<crypto::Keccak256>(), nullptr, nullptr);
    return std::make_shared<bcostars::protocol::TransactionReceiptFactoryImpl>(suite);
}

/// evmc::hex yields lowercase without the 0x prefix, which is exactly the FISCO receipt form.
std::string expectedAddress(evmc::address const& sender, uint64_t nonce)
{
    return evm::toFiscoContractAddress(sender, nonce);
}
}  // namespace

BOOST_AUTO_TEST_SUITE(EthL1ReceiptContractAddressTest)

/// A CREATE uses the pre-bump nonce: the same pair the EVM derives the account address from.
BOOST_AUTO_TEST_CASE(createReportsDerivedAddress)
{
    constexpr auto kSender = 0x6ac7ea33f8831ea9dcc53393aaa88b25a785dbf0_address;
    // Non-zero on purpose: nonce 0 and nonce N take different RLP paths (0x80 flips the length
    // prefix), so a non-zero case is what proves the value is threaded through rather than
    // defaulted.
    constexpr uint64_t kNonce = 0x80;

    auto factory = makeReceiptFactory();
    StubHost host;
    evmc::Result result{EVMC_SUCCESS, 100000};

    auto receipt = EthL1Policy::buildReceipt(host, result, /*gasUsed=*/21000, *factory,
        /*blockNumber=*/7, /*keepOutput=*/false, /*isCreate=*/true, kSender, kNonce);

    auto const reported = std::string{receipt->contractAddress()};
    BOOST_REQUIRE_EQUAL(reported, expectedAddress(kSender, kNonce));

    // The wire form the RPC layer expects: 40 lowercase hex characters, no prefix.
    BOOST_REQUIRE_EQUAL(reported.size(), 40U);
    BOOST_CHECK(reported.find("0x") == std::string::npos);
    for (char const c : reported)
    {
        BOOST_CHECK((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
    }

    // Cross-checked against the Yellow-Paper CREATE vector for this sender at nonce 0, so the
    // helper is not merely self-consistent.
    BOOST_CHECK_EQUAL(expectedAddress(kSender, 0), "cd234a471b72ba2f1ccf0a70fcaba648a5eecd8d");
    BOOST_CHECK_NE(reported, expectedAddress(kSender, 0));  // kNonce is not 0

    // The nonce used is the pre-bump one: passing the post-bump value must NOT reproduce it.
    BOOST_CHECK_NE(reported, expectedAddress(kSender, kNonce + 1));
}

/// A call to an existing account carries no created address.
BOOST_AUTO_TEST_CASE(callLeavesAddressEmpty)
{
    constexpr auto kSender = 0x6ac7ea33f8831ea9dcc53393aaa88b25a785dbf0_address;

    auto factory = makeReceiptFactory();
    StubHost host;
    evmc::Result result{EVMC_SUCCESS, 100000};

    auto receipt = EthL1Policy::buildReceipt(host, result, /*gasUsed=*/21000, *factory,
        /*blockNumber=*/7, /*keepOutput=*/false, /*isCreate=*/false, kSender, /*senderNonce=*/3);

    BOOST_CHECK(receipt->contractAddress().empty());
}

/// A reverted CREATE still reports the address, matching op-geth's MakeReceipt (it keys on
/// tx.To() == nil, not on status) and the OP lane's m_isCreate branch.
BOOST_AUTO_TEST_CASE(revertedCreateStillReportsAddress)
{
    constexpr auto kSender = 0x6ac7ea33f8831ea9dcc53393aaa88b25a785dbf0_address;
    constexpr uint64_t kNonce = 5;

    auto factory = makeReceiptFactory();
    StubHost host;
    evmc::Result result{EVMC_REVERT, 0};

    auto receipt = EthL1Policy::buildReceipt(host, result, /*gasUsed=*/53000, *factory,
        /*blockNumber=*/9, /*keepOutput=*/false, /*isCreate=*/true, kSender, kNonce);

    BOOST_CHECK_EQUAL(std::string{receipt->contractAddress()}, expectedAddress(kSender, kNonce));
    BOOST_CHECK_NE(receipt->status(), 0);  // the finer FISCO status, not the OP 0/1 collapse
}

BOOST_AUTO_TEST_SUITE_END()
