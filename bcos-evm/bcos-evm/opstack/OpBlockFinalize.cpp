#include <bcos-evm/adapter/StateDiffSanitize.h>
#include <bcos-evm/opstack/OpBlockFinalize.h>
#include <stdexcept>

namespace bcos::evm::opstack
{
evmone::state::StateDiff finalizeOpBlock(
    const evmone::state::StateView& view, const OpForkConfig& cfg, const evmc::address& coinbase)
{
    if (!cfg.disable_prague_requests)
        // std::runtime_error (NOT invalid_argument): the scheduler's catch ladder treats the
        // logic_error family as a local fault (-32603) but runtime_error as a block-level
        // rejection (INVALID); a Prague-request block on an OP chain is the latter. §6.4 D-4.
        throw std::runtime_error("op finalize: prague requests unsupported on OP chains");
    return bcos::evm::sanitizeStateDiff(
        view, evmone::state::finalize(view, cfg.rev, coinbase, std::nullopt, {}, {}));
}
}  // namespace bcos::evm::opstack
