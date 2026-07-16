#include <bcos-evm-ref/opstack/OpBlockFinalize.h>
#include <stdexcept>

namespace bcos::evmref::opstack
{
evmone::state::StateDiff finalizeOpBlock(
    const evmone::state::StateView& view, const OpForkConfig& cfg, const evmc::address& coinbase)
{
    if (!cfg.disable_prague_requests)
        throw std::invalid_argument("op finalize: prague requests unsupported on OP chains");
    return evmone::state::finalize(view, cfg.rev, coinbase, std::nullopt, {}, {});
}
}  // namespace bcos::evmref::opstack
