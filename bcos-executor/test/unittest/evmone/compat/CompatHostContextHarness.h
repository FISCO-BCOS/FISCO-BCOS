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
    BlsG1Add,
    BlsAll,
    Identity,
    Modexp,
    P256Verify,
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
inline void compatAttachBlsG1AddEvmPrecompile(std::shared_ptr<CompatHostTestExecutive> const& exe)
{
    auto m =
        std::make_shared<std::map<std::string, std::shared_ptr<executor::PrecompiledContract>>>();
    m->insert(
        {compatFillZeroAddr(0x0b), std::make_shared<executor::PrecompiledContract>(
                                       executor::PrecompiledRegistrar::pricer("bls12_g1add"),
                                       executor::PrecompiledRegistrar::executor("bls12_g1add"))});
    exe->setEVMPrecompiled(std::move(m));
}

inline void compatAttachBlsAllEvmPrecompile(std::shared_ptr<CompatHostTestExecutive> const& exe)
{
    auto m =
        std::make_shared<std::map<std::string, std::shared_ptr<executor::PrecompiledContract>>>();
    static const char* blsNames[] = {"bls12_g1add", "bls12_g1msm", "bls12_g2add", "bls12_g2msm",
        "bls12_pairing_check", "bls12_map_fp_to_g1", "bls12_map_fp2_to_g2"};
    for (int addr = 0x0b; addr <= 0x11; ++addr)
    {
        const char* name = blsNames[addr - 0x0b];
        m->insert({compatFillZeroAddr(addr), std::make_shared<executor::PrecompiledContract>(
                                                 executor::PrecompiledRegistrar::pricer(name),
                                                 executor::PrecompiledRegistrar::executor(name))});
    }
    exe->setEVMPrecompiled(std::move(m));
}

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

inline void compatAttachP256VerifyEvmPrecompile(std::shared_ptr<CompatHostTestExecutive> const& exe)
{
    auto m =
        std::make_shared<std::map<std::string, std::shared_ptr<executor::PrecompiledContract>>>();
    m->insert(
        {compatFillZeroAddr(0x100), std::make_shared<executor::PrecompiledContract>(
                                        executor::PrecompiledRegistrar::pricer("p256verify"),
                                        executor::PrecompiledRegistrar::executor("p256verify"))});
    exe->setEVMPrecompiled(std::move(m));
}

/// Combined helper for tests that need both BLS and p256verify registered.
/// Each compatAttach* helper creates a fresh map and calls setEVMPrecompiled, so they
/// cannot be chained — the second call replaces the first. Use this instead.
inline void compatAttachBlsAndP256VerifyEvmPrecompile(
    std::shared_ptr<CompatHostTestExecutive> const& exe)
{
    auto m =
        std::make_shared<std::map<std::string, std::shared_ptr<executor::PrecompiledContract>>>();
    static const char* blsNames[] = {"bls12_g1add", "bls12_g1msm", "bls12_g2add", "bls12_g2msm",
        "bls12_pairing_check", "bls12_map_fp_to_g1", "bls12_map_fp2_to_g2"};
    for (int addr = 0x0b; addr <= 0x11; ++addr)
    {
        const char* name = blsNames[addr - 0x0b];
        m->insert({compatFillZeroAddr(addr), std::make_shared<executor::PrecompiledContract>(
                                                 executor::PrecompiledRegistrar::pricer(name),
                                                 executor::PrecompiledRegistrar::executor(name))});
    }
    m->insert(
        {compatFillZeroAddr(0x100), std::make_shared<executor::PrecompiledContract>(
                                        executor::PrecompiledRegistrar::pricer("p256verify"),
                                        executor::PrecompiledRegistrar::executor("p256verify"))});
    exe->setEVMPrecompiled(std::move(m));
}

/// Default CallParameters matching `makeCompatHostContext` (for W1 warm tests; avoids protected
/// `getCallParameters()`). `params` must be default-constructed as MESSAGE (CallParameters is
/// non-copyable / non-movable).
inline void compatFillDefaultCallParametersForWarm(
    executor::CallParameters& params, bool createTransaction = false)
{
    params.origin = "0000000000000000000000000000000000000001";
    params.senderAddress = params.origin;
    params.receiveAddress = "0000000000000000000000000000000000000002";
    params.create = createTransaction;
    if (createTransaction)
    {
        params.receiveAddress.clear();
    }
    params.seq = 0;
}

inline executor::HostContext makeCompatHostContext(CompatHostContextFixture& fixture,
    ledger::Features const& features, CompatEvmAttach attach = CompatEvmAttach::None,
    bool createTransaction = false)
{
    task::syncWait(ledger::writeToStorage(features, *fixture.stateStorage, 1));
    auto blockContext = std::make_shared<executor::BlockContext>(fixture.stateStorage,
        fixture.ledgerCache, fixture.hashImpl, 1, h256(), 0,
        static_cast<uint32_t>(protocol::BlockVersion::MAX_VERSION), false, fixture.backend);
    // Unit-test storage may not round-trip every feature flag; pin the profile explicitly.
    blockContext->setFeatures(features);
    blockContext->setVMSchedule();
    auto executive = std::make_shared<CompatHostTestExecutive>(blockContext, "", 100, 0);
    if (attach == CompatEvmAttach::BlsG1Add)
    {
        compatAttachBlsG1AddEvmPrecompile(executive);
    }
    else if (attach == CompatEvmAttach::BlsAll)
    {
        compatAttachBlsAllEvmPrecompile(executive);
    }
    else if (attach == CompatEvmAttach::Identity)
    {
        compatAttachIdentityEvmPrecompile(executive);
    }
    else if (attach == CompatEvmAttach::Modexp)
    {
        compatAttachModexpEvmPrecompile(executive);
    }
    else if (attach == CompatEvmAttach::P256Verify)
    {
        compatAttachP256VerifyEvmPrecompile(executive);
    }
    auto callParams = std::make_unique<executor::CallParameters>(executor::CallParameters::MESSAGE);
    compatFillDefaultCallParametersForWarm(*callParams, createTransaction);
    return executor::HostContext(std::move(callParams), executive, "");
}

/// Mirror TransactionExecutive seq==0 W1 warm for harness tests (no execute()).
inline void compatExecutorEip2929WarmInitial(
    executor::HostContext& host, executor::CallParameters const& params)
{
    host.getTransactionExecutive()->warmUpEip2929InitialSet(params);
}

/// Mirror TransactionExecutive seq==0 W2 warm for harness tests.
inline void compatExecutorEip2930WarmAccessList(
    executor::HostContext& host, executor::CallParameters const& params)
{
    host.getTransactionExecutive()->warmUpEip2930AccessList(params);
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
