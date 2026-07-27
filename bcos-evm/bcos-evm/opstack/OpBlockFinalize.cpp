#include <bcos-evm/adapter/StateDiffSanitize.h>
#include <bcos-evm/opstack/OpBlockFinalize.h>
#include <stdexcept>

namespace bcos::evm::opstack
{
evmone::state::StateDiff finalizeOpBlock(
    const evmone::state::StateView& view, const OpForkConfig& cfg, const evmc::address& coinbase)
{
    if (!cfg.disable_prague_requests)
        throw std::invalid_argument("op finalize: prague requests unsupported on OP chains");
    return bcos::evm::sanitizeStateDiff(
        view, evmone::state::finalize(view, cfg.rev, coinbase, std::nullopt, {}, {}));
}
}  // namespace bcos::evm::opstack
