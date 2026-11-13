/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @brief Shared script, configurations and golden-table check for the FISCO-lane gas
 *        characterization tests (docs/adr/0012-gas-metering-policy-per-lane.md, section
 * "特征测试（PR 1）").
 *
 * The characterization tests pin what the executors charge today, before the gas rules move
 * into a policy type. Each test runs one fixed script of transactions under a list of feature
 * configurations and compares every (config, transaction) row with a table written inline in
 * the test. The table is the output of the test itself on the baseline: run the binary with
 * GAS_CHARACTERIZATION_RECORD=1 and it prints the rows instead of checking them. A refactor
 * that changes any number fails here; regenerating the table is a behaviour change and has to
 * be argued for in review as one.
 *
 * The contracts are hand-assembled so the expected costs depend on no compiler.
 */
#pragma once

#include "bcos-framework/executor/PrecompiledTypeDef.h"
#include "bcos-framework/ledger/Features.h"
#include "bcos-framework/protocol/Protocol.h"
#include <bcos-codec/abi/ContractABICodec.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/test/unit_test.hpp>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace bcos::test::gas_characterization
{
using Flag = ledger::Features::Flag;

// Runtime code of the corpus contracts.
// store: SSTORE(0, CALLDATALOAD(0))
constexpr std::string_view c_storeRuntime = "60003560005500";
// store2: SSTORE(0, CALLDATALOAD(0)) then SSTORE(0, CALLDATALOAD(32)), two writes to one slot
// in one transaction, where the storage status depends on the slot's original value
constexpr std::string_view c_store2Runtime = "60003560005560203560005500";
// revert: REVERT with the word 0x2a
constexpr std::string_view c_revertRuntime = "602a60005260206000fd";
// invalid: the INVALID opcode
constexpr std::string_view c_invalidRuntime = "fe";
// loop: JUMPDEST PUSH1 0 JUMP, runs out of gas
constexpr std::string_view c_loopRuntime = "5b600056";
// gaslimit: returns the GASLIMIT word
constexpr std::string_view c_gasLimitRuntime = "4560005260206000f3";
// proxy: STATICCALL(gas, CALLDATALOAD(0), calldata[32:]) and return the callee's return data
constexpr std::string_view c_proxyRuntime =
    "6000600036602090038060206000376000600035"
    "5afa3d600060003e3d6000f3";
// caller: CALL(gas, CALLDATALOAD(0), value 0, no input) and return the success word
constexpr std::string_view c_callerRuntime = "600060006000600060006000355af160005260206000f3";
// factory: CREATE a child whose init code returns 100 zero bytes, return the child's address
constexpr std::string_view c_factoryRuntime =
    "60056016600039"
    "600560006000f0"
    "60005260206000f3"
    "60646000f3";

// The EIP-2930 transaction of Web3AccessListResolverTest: 16 non-zero calldata bytes, two
// addresses and two storage keys in its access list, a recipient without code.
constexpr std::string_view c_accessListTx =
    "0x01f8f205078506fc23ac008357b58494811a752c8cd697e3cb27279c330ed1ada745a8d7881bc16d674ec800"
    "00906ebaf477f83e051589c1188bcc6ddccdf872f85994de0b295669a9fd93d5f28d9ec85e40f4cb697baef842"
    "a00000000000000000000000000000000000000000000000000000000000000003a00000000000000000000000"
    "000000000000000000000000000000000000000007d694bb9bc244d798123fde783fcc1c72d3bb8c189413c080"
    "a036b241b061a36a32ab7fe86c7aa9eb592dd59018cd0443adc0903590c16b02b0a05edcc541b4741c5cc6dd34"
    "7c5ed9577ef293a62787b4510465fadbfe39ee4094";

// The chain's tx_gas_limit in every test, and the gas limit the capped step signs for.
constexpr int64_t c_chainGasLimit = 3'000'000;
constexpr int64_t c_cappedGasLimit = 100'000;

// init code that returns the given runtime: CODECOPY the bytes after this 12-byte header, RETURN
inline bytes initCodeFor(std::string_view runtimeHex)
{
    auto runtime = fromHex(runtimeHex);
    auto size = static_cast<uint8_t>(runtime.size());
    bytes code{0x60, size, 0x60, 0x0c, 0x60, 0x00, 0x39, 0x60, size, 0x60, 0x00, 0xf3};
    code.insert(code.end(), runtime.begin(), runtime.end());
    return code;
}

// a 32-byte word holding the 20-byte address given in hex, left-padded
inline bytes addressWord(std::string_view addressHex)
{
    auto address = fromHex(addressHex);
    bytes word(32 - address.size(), 0);
    word.insert(word.end(), address.begin(), address.end());
    return word;
}

inline bytes uintWord(uint64_t value)
{
    bytes word(32, 0);
    for (int i = 31; i >= 24; --i)
    {
        word[i] = static_cast<uint8_t>(value & 0xff);
        value >>= 8;
    }
    return word;
}

inline bytes concat(std::initializer_list<bytes> parts)
{
    bytes out;
    for (auto const& part : parts)
    {
        out.insert(out.end(), part.begin(), part.end());
    }
    return out;
}

template <class... Args>
bytes abiCall(std::string const& signature, Args const&... args)
{
    crypto::Keccak256 keccak;
    codec::abi::ContractABICodec codec(keccak);
    return codec.abiIn(signature, args...);
}

// The first eight bytes of keccak(output || contract address), empty when both are empty; it
// tells two outputs apart without pinning their bytes in the table.
inline std::string digest(bytesConstRef output, std::string_view contractAddress)
{
    if (output.empty() && contractAddress.empty())
    {
        return {};
    }
    auto data = output.toBytes();
    data.insert(data.end(), contractAddress.begin(), contractAddress.end());
    crypto::Keccak256 keccak;
    auto hash = keccak.hash(bytesConstRef(data.data(), data.size()));
    return toHex(bytesConstRef(hash.data(), 8));
}

// ------------------------------------------------------------------------------- the script
// One step: a deployment (empty target) or a call. `target` and the input may name a contract
// an earlier step deployed. Addresses go into calldata, so a contract's address, which follows
// the block number each configuration runs at, can move a nested call's calldata cost by 12
// gas (a zero byte costs 4, a non-zero one 16): a difference of 12 between two configurations
// on a nested.* row is that, not a flag.
using Addresses = std::map<std::string, std::string, std::less<>>;
struct Step
{
    enum class Lane : uint8_t
    {
        both,
        v0,  // a value transfer: executor_version 1 has no feature_balance path in this harness
        v1,  // a transaction field executor_version 0 does not read (its gas limit, a typed tx)
    };
    Step(std::string name, std::string target, std::function<bytes(Addresses const&)> input,
        Lane lane = Lane::both, std::string value = {}, int64_t gasLimit = 0,
        bool accessListTx = false)
      : name(std::move(name)),
        target(std::move(target)),
        input(std::move(input)),
        lane(lane),
        value(std::move(value)),
        gasLimit(gasLimit),
        accessListTx(accessListTx)
    {}

    std::string name;
    std::string target;
    std::function<bytes(Addresses const&)> input;
    Lane lane;
    std::string value;
    int64_t gasLimit;   // 0: the chain's tx_gas_limit is the budget
    bool accessListTx;  // send c_accessListTx instead (executor_version 1 only)
};

inline std::function<bytes(Addresses const&)> constant(bytes input)
{
    return [input = std::move(input)](Addresses const&) { return input; };
}
inline std::function<bytes(Addresses const&)> addressOf(std::string name)
{
    return [name = std::move(name)](Addresses const& addresses) {
        auto it = addresses.find(name);
        return addressWord(it == addresses.end() ? std::string(40, '0') : it->second);
    };
}
inline std::string resolve(std::string const& target, Addresses const& addresses)
{
    auto it = addresses.find(target);
    return it == addresses.end() ? target : it->second;
}

// The transaction groups of ADR 0012, section "特征测试（PR 1）": deployments (success, the code
// size limit, a deposit only the old price can pay, init code that reverts or runs out of gas),
// calls (transfer, plain, nested, revert, EVM exceptions) and metered operations (SSTORE statuses,
// the EVM built-in precompiles, FISCO precompiles).
inline std::vector<Step> script()
{
    using Lane = Step::Lane;
    const std::string pre = "00000000000000000000000000000000000000";
    const std::string noCode = "00000000000000000000000000000000000000ee";
    const std::string crypto = "000000000000000000000000000000000000100a";
    const std::string tableManager(precompiled::TABLE_MANAGER_ADDRESS);
    const bytes abc{'a', 'b', 'c'};
    // EIP-152 input: 12 rounds, zero state and message, final block flag set
    bytes blake2f(213, 0);
    blake2f[3] = 12;
    blake2f[212] = 1;
    return {
        {"store", "", constant(initCodeFor(c_storeRuntime))},
        {"store2", "", constant(initCodeFor(c_store2Runtime))},
        {"revert", "", constant(initCodeFor(c_revertRuntime))},
        {"invalid", "", constant(initCodeFor(c_invalidRuntime))},
        {"loop", "", constant(initCodeFor(c_loopRuntime))},
        {"gaslimit", "", constant(initCodeFor(c_gasLimitRuntime))},
        {"proxy", "", constant(initCodeFor(c_proxyRuntime))},
        {"caller", "", constant(initCodeFor(c_callerRuntime))},
        {"factory", "", constant(initCodeFor(c_factoryRuntime))},
        // RETURN 0x100001 zero bytes: one over executor_version 0's 1 MiB limit from 3.2 on
        // (and over 0x40000 before), affordable in memory under the 3,000,000 budget
        {"deploy.overMaxSize", "", constant(fromHex("621000016000f3"))},
        // RETURN 0x8000 zero bytes: 655,360 gas at 20 per byte, 6,553,600 at 200
        {"deploy.depositPricey", "", constant(fromHex("6180006000f3"))},
        {"deploy.initRevert", "", constant(fromHex("60006000fd"))},
        {"deploy.initLoop", "", constant(fromHex("5b600056"))},
        {"transfer", noCode, constant({})},
        {"transfer.value", noCode, constant({}), Lane::v0, "0x7"},
        {"store.add", "store", constant(uintWord(1))},
        {"store.modify", "store", constant(uintWord(2))},
        {"store.same", "store", constant(uintWord(2))},
        {"store.delete", "store", constant(uintWord(0))},
        {"store.readd", "store", constant(uintWord(5))},
        {"store2.addDelete", "store2", constant(concat({uintWord(1), uintWord(0)}))},
        {"store2.addModify", "store2", constant(concat({uintWord(1), uintWord(2)}))},
        {"store2.modifyRestore", "store2", constant(concat({uintWord(3), uintWord(2)}))},
        {"call.revert", "revert", constant({})},
        {"call.invalid", "invalid", constant({})},
        {"call.loop", "loop", constant({})},
        {"call.loopCapped", "loop", constant({}), Lane::v1, "", c_cappedGasLimit},
        {"call.gaslimit", "gaslimit", constant({})},
        {"pre.ecrecover", "proxy", constant(concat({addressWord(pre + "01"), bytes(128)}))},
        {"pre.sha256", "proxy", constant(concat({addressWord(pre + "02"), abc}))},
        {"pre.ripemd160", "proxy", constant(concat({addressWord(pre + "03"), abc}))},
        {"pre.identity", "proxy", constant(concat({addressWord(pre + "04"), bytes(64, 0x11)}))},
        {"pre.modexp", "proxy",
            constant(concat(
                {addressWord(pre + "05"), uintWord(1), uintWord(1), uintWord(1), bytes{2, 3, 5}}))},
        {"pre.ecadd", "proxy", constant(concat({addressWord(pre + "06"), bytes(128)}))},
        {"pre.ecmul", "proxy", constant(concat({addressWord(pre + "07"), bytes(96)}))},
        {"pre.ecpairing", "proxy", constant(addressWord(pre + "08"))},
        {"pre.blake2f", "proxy", constant(concat({addressWord(pre + "09"), blake2f}))},
        {"pre.fiscoKeccak", "proxy",
            constant(concat({addressWord(crypto), abiCall("keccak256Hash(bytes)", abc)}))},
        {"nested.store", "caller", addressOf("store")},
        {"nested.revert", "caller", addressOf("revert")},
        {"nested.invalid", "caller", addressOf("invalid")},
        {"nested.noCode", "caller", constant(addressWord(noCode))},
        {"nested.create", "factory", constant({})},
        {"fisco.keccak", crypto, constant(abiCall("keccak256Hash(bytes)", abc))},
        {"fisco.createKVTable", tableManager,
            constant(abiCall("createKVTable(string,string,string)", std::string("gas_char"),
                std::string("k"), std::string("v")))},
        {"web3.accessList", "", constant({}), Lane::v1, "", 0, true},
    };
}

// ------------------------------------------------------------------------- configurations
struct Config
{
    std::string name;
    ledger::Features features;
    protocol::BlockVersion version = protocol::BlockVersion::V3_18_0_VERSION;
};

// Feature sets as governance can produce them: turning a flag on turns on what it depends on
// (Features::getFeatureDependencies), turning one off turns off what depends on it. A set that
// breaks this is not a chain state.
inline void enableWithDependencies(ledger::Features& features, Flag flag)
{
    for (auto dependency : ledger::Features::getFeatureDependencies(flag))
    {
        enableWithDependencies(features, dependency);
    }
    features.set(flag);
}

inline bool dependsOn(Flag flag, Flag dependency)
{
    for (auto each : ledger::Features::getFeatureDependencies(flag))
    {
        if (each == dependency || dependsOn(each, dependency))
        {
            return true;
        }
    }
    return false;
}

inline ledger::Features withoutFlag(ledger::Features const& features, Flag flag)
{
    ledger::Features out;
    for (auto [each, name, enabled] : features.flags())
    {
        if (enabled && each != flag && !dependsOn(each, flag))
        {
            out.set(each);
        }
    }
    return out;
}

// Every flag off; each flag on (with what it depends on); the intrinsic gas flag with each
// other flag; a 3.18.0 genesis; that genesis with each flag flipped: off when the genesis has
// it, on when it does not (the EVM forks and feature_balance are never in a genesis, a chain
// turns them on by governance); then the all-off set on each older block version given.
inline std::vector<Config> configs(std::vector<std::pair<std::string_view, Flag>> const& flags,
    std::vector<std::pair<std::string_view, protocol::BlockVersion>> const& olderVersions)
{
    std::vector<Config> out;
    out.push_back({"none", {}});
    for (auto const& [name, flag] : flags)
    {
        ledger::Features features;
        enableWithDependencies(features, flag);
        out.push_back({"none+" + std::string(name), features});
    }
    for (auto const& [name, flag] : flags)
    {
        if (flag == Flag::bugfix_tx_intrinsic_gas)
        {
            continue;
        }
        ledger::Features features;
        features.set(Flag::bugfix_tx_intrinsic_gas);
        enableWithDependencies(features, flag);
        out.push_back({"none+intrinsic+" + std::string(name), features});
    }
    ledger::Features genesis;
    genesis.setGenesisFeatures(protocol::BlockVersion::V3_18_0_VERSION);
    out.push_back({"genesis", genesis});
    for (auto const& [name, flag] : flags)
    {
        if (genesis.get(flag))
        {
            out.push_back({"genesis-" + std::string(name), withoutFlag(genesis, flag)});
        }
        else
        {
            auto features = genesis;
            enableWithDependencies(features, flag);
            out.push_back({"genesis+" + std::string(name), features});
        }
    }
    for (auto const& [name, version] : olderVersions)
    {
        out.push_back({std::string(name) + ":none", {}, version});
    }
    return out;
}

struct Row
{
    std::string_view config;
    std::string_view tx;
    int32_t status;
    int64_t gasUsed;
    std::string_view digest;
};

struct Observed
{
    int32_t status = 0;
    int64_t gasUsed = 0;
    std::string digest;
};

// (config, tx) -> what the executor produced, in script order per config
class Recorder
{
public:
    void add(std::string config, std::string tx, Observed observed)
    {
        auto key = std::pair{config, tx};
        BOOST_REQUIRE_MESSAGE(!m_rows.contains(key), "duplicate row " << config << " " << tx);
        m_order.push_back(key);
        m_rows.emplace(std::move(key), std::move(observed));
    }

    // GAS_CHARACTERIZATION_RECORD set: print the table. Otherwise every row must match.
    void check(std::vector<Row> const& expected) const
    {
        if (std::getenv("GAS_CHARACTERIZATION_RECORD") != nullptr)
        {
            BOOST_TEST_MESSAGE(
                "GAS_CHARACTERIZATION_RECORD is set: printing the table, not "
                "checking it");
            for (auto const& key : m_order)
            {
                auto const& row = m_rows.at(key);
                std::cout << "        {\"" << key.first << "\", \"" << key.second << "\", "
                          << row.status << ", " << row.gasUsed << ", \"" << row.digest << "\"},\n";
            }
            return;
        }
        BOOST_CHECK_EQUAL(expected.size(), m_rows.size());
        for (auto const& row : expected)
        {
            BOOST_TEST_INFO_SCOPE(row.config << " " << row.tx);
            auto it = m_rows.find({std::string(row.config), std::string(row.tx)});
            if (it == m_rows.end())
            {
                BOOST_ERROR("expected row not produced");
                continue;
            }
            BOOST_CHECK_EQUAL(it->second.status, row.status);
            BOOST_CHECK_EQUAL(it->second.gasUsed, row.gasUsed);
            BOOST_CHECK_EQUAL(it->second.digest, row.digest);
        }
    }

private:
    std::vector<std::pair<std::string, std::string>> m_order;
    std::map<std::pair<std::string, std::string>, Observed> m_rows;
};
}  // namespace bcos::test::gas_characterization
