#pragma once
// In-house JSON(pre)->ledger seeding on the bcos-evm-free lane: the vector pre
// (jsoncpp object, key=address hex, value={balance,nonce,code,storage}) is written
// account-by-account through the same EVMAccount path the Ethereum/OP executors use
// (eth::ethViewAccount — the eth-lane table-name rule), so a seeded view is row-for-row
// what execution reads. Nonce/balance zero values are written as explicit "0" rows; the
// MPT build reads a missing row as the same Yellow Paper default, so the stateRoot is
// unaffected. Empty code leaves NO code rows (a missing CODE_HASH reads as
// emptyCodeHash()).
#include <bcos-crypto/hash/Keccak256.h>  // keccak256Hash
#include <bcos-framework/ledger/EVMAccount.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <ethereum-executor/EthereumState.h>  // eth::ethViewAccount
#include <json/json.h>
#include <algorithm>  // std::copy
#include <cstdint>    // std::uint64_t
#include <evmc/evmc.hpp>
#include <iterator>  // std::begin/std::end
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>  // std::move
#include <vector>   // std::vector

namespace opstack_test
{

// bcos::fromHex left-pads an odd-length payload with a leading '0' nibble, which would turn a
// malformed test vector into a valid-length but WRONG value — reject odd-length payloads first.
inline bcos::bytes jsonHexBytes(std::string_view hex)
{
    const auto payload = (hex.starts_with("0x") || hex.starts_with("0X")) ? hex.substr(2) : hex;
    if (payload.size() % 2 != 0)
        throw std::runtime_error("odd-length hex payload: " + std::string(hex));
    return bcos::fromHex(hex);
}

inline evmc::address jsonAddress(std::string_view hex)
{
    const auto bytes = jsonHexBytes(hex);
    if (bytes.size() != sizeof(evmc::address::bytes))
        throw std::runtime_error("jsonAddress: bad length for " + std::string(hex));
    evmc::address addr;
    std::copy(bytes.begin(), bytes.end(), std::begin(addr.bytes));
    return addr;
}

inline evmc::bytes32 jsonBytes32(std::string_view hex)
{
    const auto bytes = jsonHexBytes(hex);
    if (bytes.size() != sizeof(evmc::bytes32::bytes))
        throw std::runtime_error("jsonBytes32: bad length for " + std::string(hex));
    evmc::bytes32 out;
    std::copy(bytes.begin(), bytes.end(), std::begin(out.bytes));
    return out;
}

inline evmc::bytes jsonBytes(std::string_view hex)
{
    const auto bytes = jsonHexBytes(hex);
    return {bytes.begin(), bytes.end()};
}

inline bcos::u256 jsonU256(std::string_view hex)
{
    // bcos::u256's string ctor auto-detects the 0x prefix (base-0 semantics).
    return bcos::u256(std::string(hex));
}

inline uint64_t jsonU64(std::string_view hex)
{
    // Bounds-checked: a vector nonce above uint64_t would otherwise silently truncate through
    // the narrowing cast.
    const auto v = jsonU256(hex);
    if (v > std::numeric_limits<uint64_t>::max())
        throw std::overflow_error("jsonU64: value exceeds uint64_t: " + std::string(hex));
    return static_cast<uint64_t>(v);
}

/// Writes the vector pre (jsoncpp object, key=address hex, value={balance,nonce,code,storage})
/// into an already-mutable execution view via EVMAccount. Shared by seedPreState (backend
/// seeding) and the dual-run/golden harnesses, which need the pre-state INSIDE the scanned
/// delta layer (the incremental MPT build scans the top mutable layer only).
template <class View>
void seedPreStateIntoView(View& view, Json::Value const& pre)
{
    for (auto const& addrKey : pre.getMemberNames())
    {
        auto const& acct = pre[addrKey];
        auto account = bcos::executor_v1::eth::ethViewAccount(view, jsonAddress(addrKey));
        bcos::task::syncWait(account.create());
        bcos::task::syncWait(account.setNonce(std::to_string(jsonU64(acct["nonce"].asString()))));
        bcos::task::syncWait(account.setBalance(jsonU256(acct["balance"].asString())));
        // Empty code ("0x" -> empty bytes) leaves no code rows at all: a missing CODE_HASH
        // reads as emptyCodeHash() everywhere downstream.
        if (acct.isMember("code"))
        {
            auto const codeStr = acct["code"].asString();
            if (!codeStr.empty() && codeStr != "0x")
            {
                auto const codeEvmc = jsonBytes(codeStr);
                bcos::bytes code{codeEvmc.begin(), codeEvmc.end()};
                bcos::task::syncWait(account.setCode(
                    code, {}, bcos::crypto::keccak256Hash(bcos::bytesConstRef{
                                  code.data(), code.size()})));
            }
        }
        if (acct.isMember("storage"))
        {
            for (auto const& key : acct["storage"].getMemberNames())
            {
                bcos::task::syncWait(account.setStorage(
                    jsonBytes32(key), jsonBytes32(acct["storage"][key].asString())));
            }
        }
    }
}

/// Seeds the vector pre into MLS: fork -> seedPreStateIntoView -> mergeView.
/// Precondition: the MLS deque must be empty here — mergeView merges the pushed layer only in
/// that case (MultiLayerStorage's own WARNING). That holds for this helper's use (fresh MLS,
/// single seed), so the seed lands in the backend immediately and the backend assertions can
/// pass.
template <class MLS>
void seedPreState(MLS& multiLayerStorage, Json::Value const& pre)
{
    auto view = multiLayerStorage.fork();
    view.newMutable();
    seedPreStateIntoView(view, pre);
    bcos::task::syncWait(multiLayerStorage.mergeView(std::move(view)));
}

}  // namespace opstack_test
