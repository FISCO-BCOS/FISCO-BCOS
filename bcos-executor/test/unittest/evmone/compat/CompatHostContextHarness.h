/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  @brief Shared HostContext + minimal m_evmPrecompiled wiring for Compat unit tests.
 *  @file CompatHostContextHarness.h
 */
#pragma once

#include "../../mock/MockLedger.h"
#include "CallParameters.h"
#include "bcos-executor/src/Common.h"
#include "bcos-framework/ledger/Features.h"
#include "bcos-table/src/StateStorage.h"
#include "bcos-task/Wait.h"
#include "executive/BlockContext.h"
#include "executive/TransactionExecutive.h"
#include "vm/HostContext.h"
#include "vm/Precompiled.h"
#include <iomanip>
#include <map>
#include <memory>
#include <sstream>
#include <string>

namespace bcos::test
{

enum class CompatEvmAttach
{
    None,
    Identity,
    Modexp,
};

class CompatHostTestExecutive : public executor::TransactionExecutive
{
public:
    CompatHostTestExecutive(std::shared_ptr<executor::BlockContext> blockContext,
        std::string contractAddress, int64_t contextID, int64_t seq)
      : executor::TransactionExecutive(*blockContext, std::move(contractAddress), contextID, seq),
        m_blockContextHolder(std::move(blockContext))
    {}

    executor::TransactionExecutive::Ptr buildCompatChild(int64_t seq)
    {
        return buildChildExecutive("", contextID(), seq);
    }

private:
    // TransactionExecutive stores BlockContext by reference; keep the owning shared_ptr alive.
    std::shared_ptr<executor::BlockContext> m_blockContextHolder;
};

struct CompatHostContextFixture
{
    CompatHostContextFixture()
    {
        bcos::executor::GlobalHashImpl::g_hashImpl = std::make_shared<crypto::Keccak256>();
    }

    std::shared_ptr<crypto::Keccak256> hashImpl = std::make_shared<crypto::Keccak256>();
    std::shared_ptr<storage::StateStorage> backend =
        std::make_shared<storage::StateStorage>(nullptr, false);
    std::shared_ptr<storage::StateStorage> stateStorage =
        std::make_shared<storage::StateStorage>(backend, false);
    std::shared_ptr<executor::LedgerCache> ledgerCache =
        std::make_shared<executor::LedgerCache>(std::make_shared<MockLedger>());
};

inline std::string compatFillZeroAddr(int num)
{
    std::stringstream stream;
    stream << std::setfill('0') << std::setw(40) << std::hex << num;
    return stream.str();
}

/// Bare TransactionExecutive has no m_evmPrecompiled; tests that run EVM precompiles must attach.
inline void compatAttachIdentityEvmPrecompile(std::shared_ptr<CompatHostTestExecutive> const& exe)
{
    auto m =
        std::make_shared<std::map<std::string, std::shared_ptr<executor::PrecompiledContract>>>();
    m->insert({compatFillZeroAddr(4), std::make_shared<executor::PrecompiledContract>(15, 3,
                                          executor::PrecompiledRegistrar::executor("identity"))});
    exe->setEVMPrecompiled(std::move(m));
}

inline void compatAttachModexpEvmPrecompile(std::shared_ptr<CompatHostTestExecutive> const& exe)
{
    auto m =
        std::make_shared<std::map<std::string, std::shared_ptr<executor::PrecompiledContract>>>();
    m->insert({compatFillZeroAddr(5), std::make_shared<executor::PrecompiledContract>(
                                          executor::PrecompiledRegistrar::pricer("modexp"),
                                          executor::PrecompiledRegistrar::executor("modexp"))});
    exe->setEVMPrecompiled(std::move(m));
}

inline executor::HostContext makeCompatHostContext(CompatHostContextFixture& fixture,
    ledger::Features const& features, CompatEvmAttach attach = CompatEvmAttach::None)
{
    task::syncWait(ledger::writeToStorage(features, *fixture.stateStorage, 1));
    auto blockContext = std::make_shared<executor::BlockContext>(fixture.stateStorage,
        fixture.ledgerCache, fixture.hashImpl, 1, h256(), 0,
        static_cast<uint32_t>(protocol::BlockVersion::MAX_VERSION), false, fixture.backend);
    // Unit-test storage may not round-trip every feature flag; pin the profile explicitly.
    blockContext->setFeatures(features);
    blockContext->setVMSchedule();
    auto executive = std::make_shared<CompatHostTestExecutive>(blockContext, "", 100, 0);
    if (attach == CompatEvmAttach::Identity)
    {
        compatAttachIdentityEvmPrecompile(executive);
    }
    else if (attach == CompatEvmAttach::Modexp)
    {
        compatAttachModexpEvmPrecompile(executive);
    }
    auto callParams = std::make_unique<executor::CallParameters>(executor::CallParameters::MESSAGE);
    return executor::HostContext(std::move(callParams), executive, "");
}

inline evmc_result compatCallBuiltInPrecompiled(
    executor::HostContext& host, std::string receive, bytes data, int64_t gas = 10'000'000)
{
    auto req = std::make_unique<executor::CallParameters>(executor::CallParameters::MESSAGE);
    req->origin = "0000000000000000000000000000000000000001";
    req->senderAddress = req->origin;
    req->receiveAddress = std::move(receive);
    req->data = std::move(data);
    req->gas = gas;
    return host.callBuiltInPrecompiled(req, true);
}

}  // namespace bcos::test
