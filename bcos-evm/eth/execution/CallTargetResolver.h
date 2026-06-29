#pragma once

#include "bcos-evm/eth/RevisionConfig.h"
#include "bcos-evm/eth/execution/FrameScope.h"
#include "bcos-evm/eth/state/VmHostPolicy.h"
#include <evmc/evmc.h>
#include <functional>

namespace bcos::evm
{
struct ChainCallTargetPort;
}

namespace bcos::evm::state
{
class State;
}

namespace bcos::evm::execution
{

enum class CallTargetKind
{
    EvmContract,
    BuiltinPrecompile,
    ChainPrecompile,
    EmptyAccount,
    PolicyRejected,
};

enum class WarmPolicy
{
    Never,
    TxEntryAlways,
    TxEntryIfStatic,
    FrameEntryOnly,  // CREATE warm-pin; set by resolveFrameTarget, not consumed by enumerate
};

/// Tx-entry warm set includes TxEntryAlways (builtin) and TxEntryIfStatic (fixed predeploys).
inline constexpr bool isTxEntryWarm(WarmPolicy policy) noexcept
{
    return policy == WarmPolicy::TxEntryAlways || policy == WarmPolicy::TxEntryIfStatic;
}

struct CallTargetDescriptor
{
    CallTargetKind kind{CallTargetKind::EvmContract};
    evmc_address dispatchAddress{};
    WarmPolicy warmPolicy{WarmPolicy::Never};
    evmc_message routed{};
};

CallTargetDescriptor resolveCallTarget(state::State& state,
    bcos::evm_standard::RevisionConfig const& revision, evmc_message msg, FrameScope scope,
    ChainCallTargetPort* chainPort, state::VmHostPolicy* extension);

void enumerateTxEntryWarmTargets(bcos::evm_standard::RevisionConfig const& cfg,
    ChainCallTargetPort const* chainPort, std::function<void(evmc_address const&)> const& consume);

}  // namespace bcos::evm::execution
