#pragma once

#include <evmc/evmc.hpp>
#include <test/state/host.hpp>

namespace bcos::evmref::opstack
{
struct PrecompileOverrides;

/// evmone::state::Host subclass, fixing three defects (spec §4.3):
///  1. get_tx_context: chain_id uses the configured value; when the three gas fields are 0,
///     the effective price is 0;
///  2. call: on a hit in PrecompileOverrides (including 0x100), dispatch per OP precompile
///     semantics; on a miss, fall back to the base class. The gas-override (0x100) path
///     reproduces the base Host's execute_message pre-dispatch semantics: all call-like kinds,
///     EVMC_DELEGATED excluded, only EVMC_CALL performs value/touch, rollback on failure.
///  3. access_account: addresses in the override table are always warm (op-geth statedb.Prepare
///     pre-warms all active precompiles; Isthmus includes 0x100, while the base is_precompile
///     gates 0x100 behind OSAKA) and no account is inserted (avoiding ghost empty accounts
///     entering the state diff); addresses outside the table are delegated to the base class,
///     with cold→warm transition semantics unchanged.
class OpHost : public evmone::state::Host
{
public:
    /// Lifetime contract (same as the base Host): state/block/hashes/tx are held by reference,
    /// and the caller must guarantee their lifetimes cover the entire lifetime of OpHost —
    /// passing a temporary (e.g. makeBlock()) will dangle immediately.
    OpHost(evmc_revision rev, evmc::VM& vm, evmone::state::State& state,
        const evmone::state::BlockInfo& block, const evmone::state::BlockHashes& hashes,
        const evmone::state::Transaction& tx, uint64_t chainId,
        const PrecompileOverrides* overrides) noexcept
      : evmone::state::Host{rev, vm, state, block, hashes, tx},
        m_state{state},
        m_chain_id{chainId},
        m_overrides{overrides},
        m_rev{rev},
        m_block{block},
        m_tx{tx}
    {}

    evmc::Result call(const evmc_message& msg) noexcept override;
    [[nodiscard]] evmc_tx_context get_tx_context() const noexcept override;
    evmc_access_status access_account(const evmc::address& addr) noexcept override;

private:
    evmone::state::State& m_state;
    uint64_t m_chain_id;
    const PrecompileOverrides* m_overrides;
    evmc_revision m_rev;
    const evmone::state::BlockInfo& m_block;
    const evmone::state::Transaction& m_tx;
};
}  // namespace bcos::evmref::opstack
