#pragma once

#include "bcos-evm/eth/execution/CallTargetResolver.h"
#include "bcos-evm/eth/execution/FrameScope.h"
#include "bcos-evm/eth/state/State.hpp"
#include <evmc/evmc.h>
#include <functional>
#include <optional>

namespace bcos::evm
{

struct ChainCallTargetPort
{
    virtual ~ChainCallTargetPort() = default;

    virtual std::optional<execution::CallTargetDescriptor> classifyTarget(state::State& state,
        evmc_address const& executionAddress, evmc_message const& msg,
        execution::FrameScope scope) = 0;

    virtual std::optional<evmc_result> dispatch(evmc_revision rev, evmc_message const& msg) = 0;

    virtual void forEachStaticWarmTarget(
        std::function<void(evmc_address const&)> const& consume) const = 0;
};

}  // namespace bcos::evm
