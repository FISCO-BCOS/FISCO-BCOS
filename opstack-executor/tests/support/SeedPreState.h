#pragma once
// In-house JSON(pre)->StateDiff seeding. This branch has no evmone
// test/utils/test_state.hpp; here we
// parse the vector pre with jsoncpp and build StateDiff directly, applying it via
// applyDiff(seeding=true).
#include <bcos-task/Wait.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <json/json.h>
#include <opstack-executor/Storage2State.h>
#include <algorithm>  // std::copy
#include <bcos-evm/eth/state/state_diff.hpp>
#include <cstdint>  // std::uint64_t
#include <evmc/evmc.hpp>
#include <intx/intx.hpp>
#include <iterator>  // std::begin/std::end
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>  // std::move
#include <vector>   // std::vector

namespace opstack_test
{

inline evmc::address jsonAddress(std::string_view hex)
{
    const auto bytes = bcos::fromHex(hex);
    if (bytes.size() != sizeof(evmc::address::bytes))
        throw std::runtime_error("jsonAddress: bad length for " + std::string(hex));
    evmc::address addr;
    std::copy(bytes.begin(), bytes.end(), std::begin(addr.bytes));
    return addr;
}

inline evmc::bytes32 jsonBytes32(std::string_view hex)
{
    const auto bytes = bcos::fromHex(hex);
    if (bytes.size() != sizeof(evmc::bytes32::bytes))
        throw std::runtime_error("jsonBytes32: bad length for " + std::string(hex));
    evmc::bytes32 out;
    std::copy(bytes.begin(), bytes.end(), std::begin(out.bytes));
    return out;
}

inline evmc::bytes jsonBytes(std::string_view hex)
{
    const auto bytes = bcos::fromHex(hex);
    return {bytes.begin(), bytes.end()};
}

inline intx::uint256 jsonU256(std::string_view hex)
{
    // vendored intx::from_string takes const char*/std::string (no base param) and
    // auto-detects the 0x prefix (base-0 semantics).
    return intx::from_string<intx::uint256>(std::string(hex));
}

inline uint64_t jsonU64(std::string_view hex)
{
    // Bounds-checked: a vector nonce above uint64_t would otherwise silently truncate through
    // the narrowing cast.
    const auto v = intx::from_string<intx::uint256>(std::string(hex));
    if (v > std::numeric_limits<uint64_t>::max())
        throw std::overflow_error("jsonU64: value exceeds uint64_t: " + std::string(hex));
    return static_cast<uint64_t>(v);
}

/// Seeds the vector pre (jsoncpp object, key=address hex, value={balance,nonce,code,storage})
/// into MLS: fork -> Storage2State::applyDiff(seeding=true) -> mergeView. seeding=true exempts
/// the EIP-161 empty-account guard (Storage2State::applyDiff contract).
template <class MLS>
void seedPreState(MLS& multiLayerStorage, Json::Value const& pre)
{
    evmone::state::StateDiff diff;
    diff.modified_accounts.reserve(pre.size());
    for (auto const& addrKey : pre.getMemberNames())
    {
        auto const& acct = pre[addrKey];
        evmone::state::StateDiff::Entry entry;
        entry.addr = jsonAddress(addrKey);
        entry.nonce = jsonU64(acct["nonce"].asString());
        entry.balance = jsonU256(acct["balance"].asString());
        // Empty code ("0x" -> empty bytes) stays nullopt: a has_value empty vector would write
        // extra CODE_BINARY/ABI rows (stateRoot-unobservable but needless).
        if (acct.isMember("code"))
        {
            auto const codeStr = acct["code"].asString();
            if (!codeStr.empty() && codeStr != "0x")
            {
                entry.code = jsonBytes(codeStr);
            }
        }
        if (acct.isMember("storage"))
        {
            for (auto const& key : acct["storage"].getMemberNames())
            {
                entry.modified_storage.emplace_back(
                    jsonBytes32(key), jsonBytes32(acct["storage"][key].asString()));
            }
        }
        diff.modified_accounts.push_back(std::move(entry));
    }

    auto view = multiLayerStorage.fork();
    view.newMutable();
    {
        bcos::evm::evmstate::Storage2State<typename MLS::ViewType> bridge(view);
        bridge.applyDiff(diff, /*seeding=*/true);
        if (bridge.poisoned())
        {
            throw std::runtime_error(
                "seedPreState: ledger poisoned: " + std::string(bridge.firstError()));
        }
    }
    // mergeBackStorage merges the oldest layer (FIFO). Drain the stack — the seed lands in the
    // backend immediately, and with an empty stack before each block push, mergeView persists
    // right away so the backend assertions can pass.
    bcos::task::syncWait(multiLayerStorage.mergeView(std::move(view)));
}

}  // namespace opstack_test
