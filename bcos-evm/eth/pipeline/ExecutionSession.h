#pragma once

#include "bcos-evm/eth/ExecuteMessage.h"
#include "bcos-evm/eth/pipeline/TxPipelineContext.h"
#include "bcos-evm/eth/ports/ChainCallTargetPort.h"
#include "bcos-evm/eth/state/VmHostPolicy.h"
#include <cassert>
#include <stdexcept>

namespace bcos::evm
{

/// Kernel view of per-tx execution environment (ADR-027 Tier 1+2). Chain Bundles own adapters.
struct ExecutionSession
{
    ChainCallTargetPort* chainPort{nullptr};
    state::VmHostPolicy* extension{nullptr};
    evmc::VM* vm{nullptr};
    state::BlockHashes blockHashes{};
    bool fixStorageStatus{true};
    bool fixNonceInit{false};

    void wire(TxPipelineContext& ctx) const
    {
        if (vm == nullptr)
        {
            throw std::invalid_argument("ExecutionSession::wire requires vm");
        }

        ctx.extension = extension;
        ctx.chainPort = chainPort;
        ctx.inputs.vm = vm;
        ctx.session = this;

#ifndef NDEBUG
        assert(ctx.extension == extension);
        assert(ctx.chainPort == chainPort);
        assert(ctx.inputs.vm == vm);
#endif
    }

    ExecuteMessageInput toExecuteMessageInput(TxPipelineContext const& ctx) const
    {
        ExecuteMessageInput input;
        input.state = const_cast<state::State*>(&ctx.state);
        input.vm = vm;
        input.message = ctx.message;
        input.gasPrice = ctx.gasPrice;
        input.blockInfo = ctx.inputs.blockInfo;
        input.blockHashes = blockHashes;
        input.revisionConfig = ctx.revisionConfig;
        input.txProps = ctx.txProps;
        input.accessList = ctx.inputs.accessList;
        input.authorizationListPresent = ctx.inputs.authorizationListPresent;
        input.authorizations = ctx.inputs.authorizations;
        input.web3TypedTxKind = ctx.inputs.web3TypedTxKind;
        input.extension = extension;
        input.chainPort = chainPort;
        input.fixStorageStatus = fixStorageStatus;
        input.fixNonceInit = fixNonceInit;
        return input;
    }
};

}  // namespace bcos::evm
