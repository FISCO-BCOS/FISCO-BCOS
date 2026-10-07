#include "PrecompiledManager.h"
#include "bcos-executor/src/precompiled/BFSPrecompiled.h"
#include "bcos-executor/src/precompiled/CastPrecompiled.h"
#include "bcos-executor/src/precompiled/ConsensusPrecompiled.h"
#include "bcos-executor/src/precompiled/CryptoPrecompiled.h"
#include "bcos-executor/src/precompiled/KVTablePrecompiled.h"
#include "bcos-executor/src/precompiled/ShardingPrecompiled.h"
#include "bcos-executor/src/precompiled/SystemConfigPrecompiled.h"
#include "bcos-executor/src/precompiled/TableManagerPrecompiled.h"
#include "bcos-executor/src/precompiled/TablePrecompiled.h"
#include "bcos-executor/src/precompiled/extension/AccountManagerPrecompiled.h"
#include "bcos-executor/src/precompiled/extension/AccountPrecompiled.h"
#include "bcos-executor/src/precompiled/extension/AuthManagerPrecompiled.h"
#include "bcos-executor/src/precompiled/extension/BalancePrecompiled.h"
#include "bcos-executor/src/precompiled/extension/ContractAuthMgrPrecompiled.h"
#include "bcos-executor/src/precompiled/extension/DagTransferPrecompiled.h"
#include "bcos-executor/src/precompiled/extension/GroupSigPrecompiled.h"
#include "bcos-executor/src/precompiled/extension/PaillierPrecompiled.h"
#include "bcos-executor/src/precompiled/extension/RingSigPrecompiled.h"
#include "bcos-executor/src/precompiled/extension/ZkpPrecompiled.h"
#include "bcos-transaction-executor/precompiled/EvmPrecompiledAdapter.h"
#include "bcos-transaction-executor/precompiled/PrecompiledImpl.h"
#include <memory>
#include <range/v3/algorithm/sort.hpp>


bcos::executor_v1::PrecompiledManager::PrecompiledManager(crypto::Hash::Ptr hashImpl)
  : m_hashImpl(std::move(hashImpl))
{
    // EVM built-in precompiles: the execute functions come from the shared
    // eth::evm implementation (ethereum-executor/EVMPrecompiles.h) via the
    // adapters in EvmPrecompiledAdapter.h — one canonical implementation for
    // both executors. Pricing stays on the BCOS side (fixed constants /
    // registrar pricers below), and ecrecover (0x01) / modexp (0x05) keep
    // their legacy bcos-evm executors because their edge semantics differ
    // (see EvmPrecompiledAdapter.h's file comment).
    m_address2Precompiled.emplace_back(
        1, Precompiled{executor::PrecompiledContract(
                           3000, 0, executor::PrecompiledRegistrar::executor("ecrecover")),
               0});
    m_address2Precompiled.emplace_back(
        2, Precompiled{executor::PrecompiledContract(
                           60, 12, adaptEvmPrecompiled(eth_evm::sha256_execute, 32, 0)),
               0});
    m_address2Precompiled.emplace_back(
        3, Precompiled{executor::PrecompiledContract(
                           600, 120, adaptEvmPrecompiled(eth_evm::ripemd160_execute, 32, 0)),
               0});
    m_address2Precompiled.emplace_back(
        4, Precompiled{executor::PrecompiledContract(15, 3, identityExecutor), 0});
    m_address2Precompiled.emplace_back(5,
        Precompiled{executor::PrecompiledContract(executor::PrecompiledRegistrar::pricer("modexp"),
                        executor::PrecompiledRegistrar::executor("modexp")),
            0});
    m_address2Precompiled.emplace_back(
        6, Precompiled{executor::PrecompiledContract(
                           150, 0, adaptEvmPrecompiled(eth_evm::ecadd_execute, 64, 64)),
               0});
    m_address2Precompiled.emplace_back(
        7, Precompiled{executor::PrecompiledContract(
                           6000, 0, adaptEvmPrecompiled(eth_evm::ecmul_execute, 64, 64)),
               0});
    m_address2Precompiled.emplace_back(
        8, Precompiled{executor::PrecompiledContract(
                           executor::PrecompiledRegistrar::pricer("alt_bn128_pairing_product"),
                           adaptEvmPrecompiled(eth_evm::ecpairing_execute, 32, 32)),
               0});
    m_address2Precompiled.emplace_back(
        9, Precompiled{executor::PrecompiledContract(
                           executor::PrecompiledRegistrar::pricer("blake2_compression"),
                           adaptEvmPrecompiled(
                               eth_evm::blake2bf_execute, 64, 0, blake2InputOk)),
               0});

    // EIP-2537 BLS12-381 precompiles (Prague, 0x0b–0x11)
    static const struct
    {
        int address;
        const char* name;  // registrar pricer name (BCOS pricing, unchanged)
        EvmPrecompileExecute execute;
        size_t maxOutputSize;
        std::function<bool(bytesConstRef)> precheck;
    } blsPrecompiles[] = {
        {0x0b, "bls12_g1add", eth_evm::bls12_g1add_execute, 128, {}},
        {0x0c, "bls12_g1msm", eth_evm::bls12_g1msm_execute, 128, multipleOf(160)},
        {0x0d, "bls12_g2add", eth_evm::bls12_g2add_execute, 256, {}},
        {0x0e, "bls12_g2msm", eth_evm::bls12_g2msm_execute, 256, multipleOf(288)},
        {0x0f, "bls12_pairing_check", eth_evm::bls12_pairing_check_execute, 32, multipleOf(384)},
        {0x10, "bls12_map_fp_to_g1", eth_evm::bls12_map_fp_to_g1_execute, 128, {}},
        {0x11, "bls12_map_fp2_to_g2", eth_evm::bls12_map_fp2_to_g2_execute, 256, {}},
    };
    for (auto const& entry : blsPrecompiles)
    {
        m_address2Precompiled.emplace_back(entry.address,
            Precompiled{
                executor::PrecompiledContract(
                    executor::PrecompiledRegistrar::pricer(entry.name),
                    adaptEvmPrecompiled(entry.execute, entry.maxOutputSize, 0, entry.precheck)),
                ledger::Features::Flag::feature_evm_prague});
    }

    // EIP-7212 p256verify (Osaka, 0x0100)
    m_address2Precompiled.emplace_back(0x0100,
        Precompiled{
            executor::PrecompiledContract(
                executor::PrecompiledRegistrar::pricer("p256verify"), p256VerifyExecutor),
            ledger::Features::Flag::feature_evm_osaka});

    m_address2Precompiled.emplace_back(
        0x1000, std::make_shared<precompiled::SystemConfigPrecompiled>(m_hashImpl));
    m_address2Precompiled.emplace_back(
        0x1003, std::make_shared<precompiled::ConsensusPrecompiled>(m_hashImpl));
    m_address2Precompiled.emplace_back(
        0x1002, std::make_shared<precompiled::TableManagerPrecompiled>(m_hashImpl));
    m_address2Precompiled.emplace_back(
        0x1009, std::make_shared<precompiled::KVTablePrecompiled>(m_hashImpl));
    m_address2Precompiled.emplace_back(
        0x1001, std::make_shared<precompiled::TablePrecompiled>(m_hashImpl));
    m_address2Precompiled.emplace_back(
        0x100c, std::make_shared<precompiled::DagTransferPrecompiled>(m_hashImpl));
    m_address2Precompiled.emplace_back(
        0x100a, std::make_shared<precompiled::CryptoPrecompiled>(m_hashImpl));
    m_address2Precompiled.emplace_back(
        0x100e, std::make_shared<precompiled::BFSPrecompiled>(m_hashImpl));
    m_address2Precompiled.emplace_back(
        0x5003, Precompiled{std::make_shared<precompiled::PaillierPrecompiled>(m_hashImpl),
                    ledger::Features::Flag::feature_paillier});
    m_address2Precompiled.emplace_back(
        0x5004, std::make_shared<precompiled::GroupSigPrecompiled>(m_hashImpl));
    m_address2Precompiled.emplace_back(
        0x5005, std::make_shared<precompiled::RingSigPrecompiled>(m_hashImpl));
    m_address2Precompiled.emplace_back(
        0x5100, std::make_shared<precompiled::ZkpPrecompiled>(m_hashImpl));
    m_address2Precompiled.emplace_back(
        0x1005, std::make_shared<precompiled::AuthManagerPrecompiled>(m_hashImpl));
    m_address2Precompiled.emplace_back(
        0x10002, std::make_shared<precompiled::ContractAuthMgrPrecompiled>(m_hashImpl));
    m_address2Precompiled.emplace_back(
        0x1010, Precompiled{std::make_shared<precompiled::ShardingPrecompiled>(m_hashImpl),
                    ledger::Features::Flag::feature_sharding});
    m_address2Precompiled.emplace_back(
        0x100f, std::make_shared<precompiled::CastPrecompiled>(m_hashImpl));
    m_address2Precompiled.emplace_back(
        0x10003, std::make_shared<precompiled::AccountManagerPrecompiled>(m_hashImpl));
    m_address2Precompiled.emplace_back(
        0x10004, std::make_shared<precompiled::AccountPrecompiled>(m_hashImpl));
    m_address2Precompiled.emplace_back(
        0x1011, Precompiled{std::make_shared<precompiled::BalancePrecompiled>(m_hashImpl),
                    ledger::Features::Flag::feature_balance_precompiled});

    ::ranges::sort(m_address2Precompiled,
        [](const auto& lhs, const auto& rhs) { return std::get<0>(lhs) < std::get<0>(rhs); });

    // Init the AUTH_COMMITTEE_ADDRESS
}

bcos::executor_v1::Precompiled const* bcos::executor_v1::PrecompiledManager::getPrecompiled(
    unsigned long contractAddress) const
{
    auto it = std::lower_bound(m_address2Precompiled.begin(), m_address2Precompiled.end(),
        contractAddress,
        [](const decltype(m_address2Precompiled)::value_type& lhs, unsigned long rhs) {
            return std::get<0>(lhs) < rhs;
        });
    if (it != m_address2Precompiled.end() && std::get<0>(*it) == contractAddress)
    {
        return std::addressof(std::get<1>(*it));
    }

    return nullptr;
}

bcos::executor_v1::Precompiled const* bcos::executor_v1::PrecompiledManager::getPrecompiled(
    const evmc_address& address) const
{
    constexpr static unsigned long MAX_PRECOMPILED_ADDRESS = 100000;

    u160 intAddress;
    boost::multiprecision::import_bits(
        intAddress, address.bytes, address.bytes + sizeof(address.bytes));
    if (intAddress > 0 && intAddress < MAX_PRECOMPILED_ADDRESS)
    {
        auto addressUL = intAddress.convert_to<unsigned long>();
        return getPrecompiled(addressUL);
    }

    return nullptr;
}

// FIB-84: feature-aware lookup, gated by bugfix_precompiled_feature_gate
// When the bugfix flag is off, returns unconditionally (pre-fix behavior) to preserve
// replay of historical blocks that ran with feature flags improperly enforced.
bcos::executor_v1::Precompiled const* bcos::executor_v1::PrecompiledManager::getPrecompiled(
    unsigned long contractAddress, const ledger::Features& features) const
{
    const auto* precompiled = getPrecompiled(contractAddress);
    if (precompiled == nullptr)
    {
        return nullptr;
    }
    if (!features.get(ledger::Features::Flag::bugfix_precompiled_feature_gate))
    {
        return precompiled;
    }
    if (const auto flag = featureFlag(*precompiled); flag && !features.get(*flag))
    {
        return nullptr;
    }
    return precompiled;
}

bcos::executor_v1::Precompiled const* bcos::executor_v1::PrecompiledManager::getPrecompiled(
    const evmc_address& address, const ledger::Features& features) const
{
    const auto* precompiled = getPrecompiled(address);
    if (precompiled == nullptr)
    {
        return nullptr;
    }
    if (!features.get(ledger::Features::Flag::bugfix_precompiled_feature_gate))
    {
        return precompiled;
    }
    if (const auto flag = featureFlag(*precompiled); flag && !features.get(*flag))
    {
        return nullptr;
    }
    return precompiled;
}
