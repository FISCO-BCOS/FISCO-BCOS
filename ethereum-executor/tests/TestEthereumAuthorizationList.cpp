/// @file TestEthereumAuthorizationList.cpp
/// @brief EIP-7702 cross-chain authorization rejection, pinned against
///        eth_transition_detail::processAuthorizationList (EthereumTransition.h).
///
/// Ported from the retired bcos-evm/test/eth/EthTransitionTest.cpp's
/// AuthorizationForAnotherChainIsSkipped (step 4.2): step 1 of the EIP-7702
/// authorization processing must compare each authorization's chain id against
/// the NODE's chain id. validate_transaction never checks tx.chain_id, so if the
/// tx-carried value were trusted instead, the comparison would be
/// "user input == user input" — an authorization signed for chain X would write
/// a delegation on chain Y by simply setting tx.chain_id = X. The golden
/// signature below is signed for chain_id = 1; on a node running chain 999 the
/// delegation must NOT be written.

#include <boost/test/unit_test.hpp>

#include "ethereum-executor/EthereumState.h"
#include "ethereum-executor/EthereumTransition.h"
#include "ethereum-executor/tests/TestMemoryStorage.h"
#include "bcos-tars-protocol/protocol/TransactionImpl.h"

#include <evmc/evmc.hpp>
#include <algorithm>

using namespace bcos;
using namespace bcos::executor_v1;
using namespace evmc::literals;

namespace
{
// EIP-7702 golden values: signed with eth-account per
// keccak256(0x05 || rlp([chain_id, address, nonce])).
// Private key 0x59c6995e998f97a5a0044966f0945389dc9e86dae88c7a8412f4603b6b78690d,
// signature covers chain_id = 1, addr = kDelegate, nonce = 0. Do not hand-edit.
constexpr auto kAuthority = 0x70997970C51812dc3A010C7d01b50e0d17dc79C8_address;
constexpr auto kDelegate = 0x00000000000000000000000000000000000000cc_address;
constexpr std::string_view kAuthR =
    "0x8bd0c047683d78ac6855fd9997e17dd64c4941334308c2708930682e1831c42a";
constexpr std::string_view kAuthS =
    "0x7399ba8d6bdec8bacec1cfb93d1f1bd00bedbade84959bda53464acaaa32f330";

/// The delegation designator prefix (EIP-7702): 0xef0100 || address.
bool isDelegationDesignator(evmc::bytes_view code, evmc::address const& expected) noexcept
{
    return code.size() == 23 && code[0] == 0xef && code[1] == 0x01 && code[2] == 0x00 &&
           std::equal(code.begin() + 3, code.end(), std::begin(expected.bytes));
}

/// A Web3 set-code transaction carrying one authorization mirror signed for chain 1.
/// Only authorizationList() is read by processAuthorizationList; the rest of the
/// transaction is inert for this test.
bcostars::protocol::TransactionImpl makeAuthTx()
{
    bcostars::protocol::TransactionImpl tx;
    auto& inner = tx.mutableInner();
    inner.type = static_cast<tars::Char>(1);  // TransactionType::Web3Transaction
    inner.web3TypedTxKind = static_cast<tars::Char>(4);  // EIP-7702 set-code

    bcostars::AuthorizationEntry entry;
    entry.chainID = 1;  // signed for chain 1 — the attacker's claim
    entry.address = "00000000000000000000000000000000000000cc";
    entry.nonce = 0;
    entry.r = std::string{kAuthR};
    entry.s = std::string{kAuthS};
    entry.v = 0;
    inner.data.authorizationList.emplace_back(std::move(entry));
    return tx;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(EthereumAuthorizationListTest)

BOOST_AUTO_TEST_CASE(AuthorizationForAnotherChainIsSkipped)
{
    // Fills `state` by processing the golden authorization under `nodeChainId`.
    const auto runOnChain = [](eth::EthereumState<MutableStorage>& state, uint64_t nodeChainId) {
        const auto tx = makeAuthTx();
        eth::eth_transition_detail::processAuthorizationList(state, nodeChainId, tx);
    };

    // Positive control: on chain 1 the authorization holds, the delegation
    // designator is written and the authority's nonce advances (EIP-7702 step 11).
    // Without this arm the negative assertions below could pass for an unrelated
    // reason (e.g. ecrecover failing outright).
    {
        MutableStorage storage;
        eth::EthereumState state{storage};
        runOnChain(state, 1);
        const auto code = state.get_code(kAuthority);
        BOOST_REQUIRE_MESSAGE(isDelegationDesignator(code, kDelegate),
            "chain 1: delegation designator (0xef0100||addr) must be written");
        BOOST_CHECK_EQUAL(state.get(kAuthority).nonce, 1u);
    }

    // Discriminating assertion: the node runs chain 999, the authorization was
    // only ever signed for chain 1 — it must be skipped entirely: the authority
    // account is not even created (step 1 continues before get_or_insert).
    {
        MutableStorage storage;
        eth::EthereumState state{storage};
        runOnChain(state, 999);
        BOOST_CHECK_MESSAGE(state.find(kAuthority) == nullptr,
            "cross-chain authorization must not touch the authority account");
        BOOST_CHECK_MESSAGE(state.get_code(kAuthority).empty(),
            "cross-chain authorization must NOT write a delegation designator");
        for (auto const& [addr, account] : state.modified())
        {
            BOOST_CHECK_MESSAGE(account.code.empty() ||
                                    !isDelegationDesignator(account.code, kDelegate),
                "no account may receive a delegation designator on a foreign chain");
        }
    }
}

BOOST_AUTO_TEST_SUITE_END()
