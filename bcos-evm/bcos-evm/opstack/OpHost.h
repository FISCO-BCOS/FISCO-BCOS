#pragma once

#include <bcos-evm/eth/state/host.hpp>
#include <evmc/evmc.hpp>

namespace bcos::evm::opstack
{
struct PrecompileOverrides;

/// evmone::state::Host subclass that fixes three defects (spec §4.3):
///  1. get_tx_context: chain_id uses the configured value; when all three gas fields = 0 the
///     effective price = 0.
///  2. call: when a PrecompileOverrides entry (incl. 0x100) is hit, dispatch under OP precompile
///     semantics; on a miss fall back to the base class. The gas-override (0x100) path replicates
///     the upstream execute_message pre-dispatch semantics: all call-like kinds, EVMC_DELEGATED
///     excluded, only EVMC_CALL performs value/touch, rollback on failure.
///  3. access_account: addresses in the override table are always warm (op-geth statedb.Prepare
///     warms every active precompile; Isthmus includes 0x100, whereas the upstream is_precompile
///     gates 0x100 at OSAKA) and are not inserted (avoids a ghost empty account entering the
///     state diff); addresses outside the table delegate to the base class, leaving the
///     cold→warm transition semantics unchanged.
class OpHost : public evmone::state::Host
{
public:
    /// Lifetime contract (same as the upstream Host): state/block/hashes/tx are held by reference,
    /// so the caller must guarantee they outlive the entire OpHost lifetime -- passing a temporary
    /// (e.g. makeBlock()) would dangle immediately.
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
    // TODO(vendored-base-refactor): m_state / m_rev / m_block / m_tx duplicate members the base
    // already holds. They are not redundant by choice — evmone's Host declares them before its
    // first access specifier, i.e. private (host.hpp), so a derived class cannot reach them, and
    // re-deriving them from the public interface is not possible either. Correctness is not at
    // risk (both names bind the same object and references cannot be rebound; the cost is 32
    // bytes per Host, constructed once per transaction), but the duplication is a wart worth
    // removing.
    //
    // The fix is to promote those members to protected in the vendored eth/state/host.hpp and
    // drop the copies here. That is deliberately not done in isolation: eth/state is a
    // byte-identical copy of evmone v0.21.0 and every local edit widens the diff that has to be
    // re-applied on the next upstream sync. It belongs with the vendored-code work already
    // scheduled around TODO(eth-utils-removal), where the divergence is being handled as a whole.
    evmone::state::State& m_state;
    uint64_t m_chain_id;
    const PrecompileOverrides* m_overrides;
    evmc_revision m_rev;
    const evmone::state::BlockInfo& m_block;
    const evmone::state::Transaction& m_tx;
};
}  // namespace bcos::evm::opstack
