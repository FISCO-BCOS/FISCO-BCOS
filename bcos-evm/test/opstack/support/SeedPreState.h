#pragma once
// W6 自研 JSON(pre)→StateDiff 播种。本分支无 evmone test/utils/test_state.hpp（LedgerSeed.h 的
// seedFromTestState 依赖它），这里用 jsoncpp 解析向量 pre，直接构 StateDiff 走 applyDiff(seeding=true)。
#include <bcos-evm/eth/state/state_diff.hpp>
#include <bcos-evm/ledger/Storage2Ledger.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <json/json.h>
#include <evmc/evmc.hpp>
#include <intx/intx.hpp>
#include <algorithm>  // std::copy
#include <cstdint>    // std::uint64_t
#include <iterator>   // std::begin/std::end
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>    // std::move
#include <vector>     // std::vector

namespace w6test
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
    // auto-detects the 0x prefix — equivalent to the brief's from_string(hex, 0).
    return intx::from_string<intx::uint256>(std::string(hex));
}

inline uint64_t jsonU64(std::string_view hex)
{
    return static_cast<uint64_t>(intx::from_string<intx::uint256>(std::string(hex)));
}

/// 把向量 pre（jsoncpp object，key=地址 hex，value={balance,nonce,code,storage}）
/// 播进 MLS：fork → Storage2Ledger::applyDiff(seeding=true) → pushView。
/// `Storage2Ledger::applyDiff`（Storage2Ledger.h:275）签名确认；seeding=true 豁免
/// EIP-161 空账户守卫（LedgerSeed.h 同款契约）。
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
        // 空 code（"0x" → 空字节）留 nullopt，与 LedgerSeed.h 契约③逐字对齐（R2-B：has_value 空 vector
        // 会多写 CODE_BINARY/ABI 三行但 stateRoot 不可观察，此处仍按契约写）
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
        bcos::evm::ledger::Storage2Ledger<typename MLS::ViewType> bridge(view);
        bridge.applyDiff(diff, /*seeding=*/true);
        if (bridge.poisoned())
        {
            throw std::runtime_error("seedPreState: ledger poisoned: " + std::string(bridge.firstError()));
        }
    }
    multiLayerStorage.pushView(std::move(view));
}

}  // namespace w6test
