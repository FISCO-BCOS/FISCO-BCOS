/// @file TestEthereumStateSmoke.cpp
/// Compile/instantiation smoke coverage for the split-2/4 orphan headers
/// (EVMSupport.h / EthereumState.h). Nothing else in the tree includes them
/// yet, so this target is the first place a syntax, template-instantiation or
/// ODR error in those headers shows up.
///
/// Golden values are the canonical Ethereum vectors: keccak256(""), the
/// Yellow-Paper CREATE example, the EIP-1014 CREATE2 examples, the EIP-4844
/// blob-gas vector, and the EIP-7702 authority-recovery vector shared with
/// bcos-evm/test/opstack/Op7702Test.cpp.
///
/// Checks are NDEBUG-independent (CHECK exits non-zero on failure): CI builds
/// with Release/-DNDEBUG, which would compile out assert() and leave the
/// helpers unused under -Werror. The target is registered with add_test() so
/// ctest actually runs it.

#include "ethereum-executor/EVMSupport.h"
#include "ethereum-executor/EthereumHost.h"
#include "ethereum-executor/EthereumState.h"
#include "ethereum-executor/tests/TestMemoryStorage.h"

#include "bcos-task/TBBWait.h"
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

// pr-3 adds EthereumHost.h as another orphan header: merely including it parses
// the template but instantiates nothing, so CI would not catch a member-body
// error. An explicit instantiation pulls every evmc::Host override into the
// vtable plus the private non-virtuals (create / prepare_message / call), and
// is cheaper than constructing an object (which needs an evmc::VM and a
// protocol::Transaction).
template class bcos::executor_v1::eth::EthereumHost<bcos::executor_v1::MutableStorage>;

namespace
{
int g_failures = 0;

void checkImpl(bool ok, const char* expr, const char* file, int line)
{
    if (!ok)
    {
        std::cerr << file << ':' << line << ": CHECK failed: " << expr << '\n';
        ++g_failures;
    }
}
}  // namespace

/// NDEBUG-independent check (see file header).
#define CHECK(expr) checkImpl(static_cast<bool>(expr), #expr, __FILE__, __LINE__)

namespace eth = bcos::executor_v1::eth;
namespace eth_evm = bcos::executor_v1::eth::evm;

namespace
{
/// Parse a 0x-prefixed 40-hex-digit string into an evmc::address.
evmc::address addressFromHex(const std::string& hex)
{
    const auto start = (hex.rfind("0x", 0) == 0) ? 2u : 0u;
    evmc::address out{};
    for (size_t i = 0; i < 40; ++i)
    {
        const auto c = hex[start + i];
        const auto v =
            (c <= '9') ? static_cast<uint8_t>(c - '0') : static_cast<uint8_t>((c | 32) - 'a' + 10);
        out.bytes[i / 2] = static_cast<uint8_t>((out.bytes[i / 2] << 4) | v);
    }
    return out;
}

/// Parse a 0x-prefixed 64-hex-digit string into an evmc::bytes32.
evmc::bytes32 bytes32FromHex(const std::string& hex)
{
    const auto start = (hex.rfind("0x", 0) == 0) ? 2u : 0u;
    evmc::bytes32 out{};
    for (size_t i = 0; i < 64; ++i)
    {
        const auto c = hex[start + i];
        const auto v =
            (c <= '9') ? static_cast<uint8_t>(c - '0') : static_cast<uint8_t>((c | 32) - 'a' + 10);
        out.bytes[i / 2] = static_cast<uint8_t>((out.bytes[i / 2] << 4) | v);
    }
    return out;
}

bool sameAddress(const evmc::address& a, const evmc::address& b)
{
    return std::memcmp(a.bytes, b.bytes, sizeof(a.bytes)) == 0;
}

void testU256EvmcRoundTrip()
{
    const bcos::u256 values[] = {bcos::u256(0), bcos::u256(1), bcos::u256(42),
        bcos::u256("0x1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef"),
        bcos::u256("0xffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff")};
    for (const auto& v : values)
    {
        const auto be = eth_evm::toEvmcBE(v);
        CHECK(eth_evm::fromEvmcBE(be) == v);
    }
    // Spot-check the big-endian mapping: 1 -> 0x00..01.
    CHECK(eth_evm::toEvmcBE(bcos::u256(1)) == evmc::bytes32{1});
}

void testKeccak256()
{
    const auto h = eth_evm::keccak256({});
    const auto expected =
        bytes32FromHex("0xc5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470");
    CHECK(h == expected);
}

void testCreateAddress()
{
    // Yellow-Paper / evmone canonical example:
    // CREATE(sender = 0x6ac7ea33f8831ea9dcc53393aaa88b25a785dbf0, nonce = 0)
    //   -> 0xcd234a471b72ba2f1ccf0a70fcaba648a5eecd8d
    const auto sender = addressFromHex("0x6ac7ea33f8831ea9dcc53393aaa88b25a785dbf0");
    const auto expected = addressFromHex("0xcd234a471b72ba2f1ccf0a70fcaba648a5eecd8d");
    CHECK(sameAddress(eth_evm::compute_create_address(sender, 0), expected));

    // RLP nonce-boundary coverage: single-byte 0x7f stays raw (0x7f), 0x80 flips
    // to 0x81 0x80 — the transition where hand-rolled RLP usually breaks. Values
    // re-derived with an independent RLP/keccak implementation.
    CHECK(sameAddress(eth_evm::compute_create_address(sender, 0x7f),
        addressFromHex("0x06d9a77f5e4b311bae8d559db9cdb4df94104aa0")));
    CHECK(sameAddress(eth_evm::compute_create_address(sender, 0x80),
        addressFromHex("0x08e190dcb7b73f5fcdabb43e102215c83659a76d")));
    CHECK(sameAddress(eth_evm::compute_create_address(sender, 0xff),
        addressFromHex("0x3ef7c1a519e4b4431e317d7839340e3139b03c65")));
    CHECK(sameAddress(eth_evm::compute_create_address(sender, 0x100),
        addressFromHex("0x3837c1ae70354f670550c746580199ac6a73cb0a")));
    CHECK(sameAddress(eth_evm::compute_create_address(sender, 0xffffffffffffffff),
        addressFromHex("0x9bc924993b60399df164c3763a964301d3db95ca")));
}

void testCreate2Address()
{
    // EIP-1014 examples 0, 1 and 3.
    const evmc::address zero20{};
    const evmc::bytes32 zeroSalt{};
    const uint8_t init00[] = {0x00};
    const uint8_t initDeadbeef[] = {0xde, 0xad, 0xbe, 0xef};

    CHECK(sameAddress(eth_evm::compute_create2_address(zero20, zeroSalt, {init00, 1}),
        addressFromHex("0x4D1A2e2bB4F88F0250f26Ffff098B0b30B26BF38")));

    const auto deadbeef = addressFromHex("0xdeadbeef00000000000000000000000000000000");
    CHECK(sameAddress(eth_evm::compute_create2_address(deadbeef, zeroSalt, {init00, 1}),
        addressFromHex("0xB928f69Bb1D91Cd65274e3c79d8986362984fDA3")));

    CHECK(sameAddress(eth_evm::compute_create2_address(zero20, zeroSalt, {initDeadbeef, 4}),
        addressFromHex("0x70f2b2914A2a4b783FaEFb75f459A580616Fcb5e")));
}

void testBlobGasPrice()
{
    // EIP-4844 mainnet schedule: fake_exponential(1, excess, 3338477).
    const eth_evm::BlobParams params{3, 6, 3338477};
    CHECK(eth_evm::compute_blob_gas_price(params, 0) == 1);
    CHECK(eth_evm::compute_blob_gas_price(params, 16777216) == 152);        // 2**24
    CHECK(eth_evm::compute_blob_gas_price(params, 33554432) == 23174);      // 2**25
    CHECK(eth_evm::compute_blob_gas_price(params, 67108864) == 537070730);  // 2**26
    CHECK(eth_evm::max_blob_gas_per_block(params) == 6 * eth_evm::GAS_PER_BLOB);

    // Degenerate schedule (base_fee_update_fraction == 0) must not divide by
    // zero; the guard returns the maximum like the overflow path.
    const eth_evm::BlobParams degenerate{0, 0, 0};
    CHECK(eth_evm::compute_blob_gas_price(degenerate, 0) == std::numeric_limits<bcos::u256>::max());
}

void testRecoverAuthority()
{
    // EIP-7702 golden vector (bcos-evm/test/opstack/Op7702Test.cpp):
    // private key 0x59c6995e..., chain_id = 1, delegation = 0x00..cc, nonce = 0
    //   -> authority 0x70997970C51812dc3A010C7d01b50e0d17dc79C8
    bcos::protocol::Authorization auth;
    auth.chainId = 1;
    auth.address = bcos::Address("0x00000000000000000000000000000000000000cc");
    auth.nonce = 0;
    auth.v = 0;
    auth.r = bcos::u256("0x8bd0c047683d78ac6855fd9997e17dd64c4941334308c2708930682e1831c42a");
    auth.s = bcos::u256("0x7399ba8d6bdec8bacec1cfb93d1f1bd00bedbade84959bda53464acaaa32f330");

    const auto recovered = eth_evm::recoverAuthority(auth);
    CHECK(recovered.has_value());
    CHECK(sameAddress(*recovered, addressFromHex("0x70997970C51812dc3A010C7d01b50e0d17dc79C8")));
}

void testRecoverAuthorityRejectsBadSignature()
{
    // EIP-2: r and s must be in [1, N-1]; (0, 0) is not a valid signature, so
    // recoverAuthority must fail (covers the ecrecover failure path).
    bcos::protocol::Authorization auth;
    auth.chainId = 1;
    auth.address = bcos::Address("0x00000000000000000000000000000000000000cc");
    auth.nonce = 0;
    auth.v = 0;
    auth.r = bcos::u256(0);
    auth.s = bcos::u256(0);

    CHECK(!eth_evm::recoverAuthority(auth).has_value());
}

void testEthereumStateInstantiation()
{
    // Compile/instantiation smoke: EthereumState over the in-memory storage.
    bcos::executor_v1::MutableStorage storage;
    eth::EthereumState<decltype(storage)> state(storage);

    const auto addr = addressFromHex("0x1000000000000000000000000000000000000000");

    // Write path: instantiates applyToStorage, EVMAccount and
    // clearAccountStorage (the ODR-relevant free template).
    state.insert(addr, {});
    auto* acc = state.find(addr);
    CHECK(acc != nullptr);
    acc->nonce = 1;
    bcos::task::tbb::syncWait(state.applyToStorage(EVMC_SHANGHAI));
    CHECK(state.find(addr) != nullptr);

    // Journal machinery: checkpoint, journaled create, rollback. insert() is a
    // low-level map op (no journal entry), so rollback is exercised through the
    // journaled create path instead.
    const auto cp = state.checkpoint();
    state.journal_create(addr, /*existed=*/true);
    state.rollback(cp);
    CHECK(state.find(addr) != nullptr);
}

}  // namespace

int main()
{
    testU256EvmcRoundTrip();
    testKeccak256();
    testCreateAddress();
    testCreate2Address();
    testBlobGasPrice();
    testRecoverAuthority();
    testRecoverAuthorityRejectsBadSignature();
    testEthereumStateInstantiation();

    if (g_failures > 0)
    {
        std::cerr << "TestEthereumStateSmoke: " << g_failures << " check(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "TestEthereumStateSmoke: all checks passed" << std::endl;
    return EXIT_SUCCESS;
}
