/**
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
 * @file TestEvmoneVmReuse.cpp
 * @brief Settles the "thread_local reuse leaves a dirty stack" note by experiment.
 *
 * VMInstance.cpp records, from the evmone 0.21 upgrade, that a reused VM leaves a dirty stack
 * after reset() and the next execution dies with EVMC_BAD_JUMP_DESTINATION. threadLocalEvmoneVm
 * depends on the opposite -- that reset() is clean, so one VM per thread can serve every
 * transaction -- and this file is the evidence rather than the assertion.
 *
 * Why the old note and this header can both be right: a VM entered *concurrently* is what
 * corrupts the pool (VM::get_execution_state resizes a plain vector and each slot keeps the host
 * context of the frame that reset it). Before EvmoneVm.h the instance was reachable from
 * whichever thread a coroutine resumed on, so two frames could be live in one VM at once and the
 * later reset() reclaimed a slot the other frame was still using -- which is what the note
 * records. Taking the VM strictly inside the synchronous span that runs the EVM removes the
 * overlap.
 *
 * The test is therefore differential. It pollutes one long-lived VM with the frame shapes that
 * leave the pool non-trivial -- a deep chain, an out-of-gas abort in a nested frame, a nested
 * revert -- then re-executes shallow contracts on that VM and requires byte-identical results
 * (status, gas left, output) from a freshly created VM. Residue shows up as a mismatch; the
 * historical one shows up as a spurious EVMC_BAD_JUMP_DESTINATION on a contract with no jump.
 *
 * Two guards keep this from being vacuous: the deep cases assert the observed call depth, so a
 * host that never nests (and never touches slots above 0) fails instead of passing silently; and
 * the polluted and fresh runs share code, input and gas, so equality is all that is measured.
 */

#include <ethereum-executor/EvmoneVm.h>
#include <boost/test/unit_test.hpp>
#include <algorithm>
#include <cstdint>
#include <evmc/evmc.hpp>
#include <evmc/mocked_host.hpp>
#include <string>
#include <string_view>
#include <vector>

using bcos::executor_v1::eth::threadLocalEvmoneVm;

namespace
{
/// The revision the executor runs the 3.18.0 Ethereum lane with.
constexpr evmc_revision kRev = EVMC_PRAGUE;
constexpr int64_t kGas = 5'000'000;
constexpr evmc_address kContract = {{0x01}};
constexpr evmc_address kSender = {{0x02}};

evmc::bytes fromHex(std::string_view _hex)
{
    auto nibble = [](char _c) -> int {
        if (_c >= '0' && _c <= '9')
            return _c - '0';
        if (_c >= 'a' && _c <= 'f')
            return _c - 'a' + 10;
        if (_c >= 'A' && _c <= 'F')
            return _c - 'A' + 10;
        return -1;
    };
    evmc::bytes out;
    out.reserve(_hex.size() / 2);
    for (size_t i = 0; i + 1 < _hex.size(); i += 2)
    {
        out.push_back(static_cast<uint8_t>((nibble(_hex[i]) << 4) | nibble(_hex[i + 1])));
    }
    return out;
}

std::string toHex(evmc::bytes_view _data)
{
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string out;
    out.reserve(_data.size() * 2);
    for (auto byte : _data)
    {
        out.push_back(kDigits[byte >> 4]);
        out.push_back(kDigits[byte & 0x0f]);
    }
    return out;
}

evmc::bytes word(uint64_t _value)
{
    evmc::bytes out(32, 0);
    for (int i = 31; i >= 0 && _value != 0; --i)
    {
        out[static_cast<size_t>(i)] = static_cast<uint8_t>(_value & 0xff);
        _value >>= 8;
    }
    return out;
}

/// Everything compared between the polluted and the fresh run. gas_left is deliberately part of
/// it: a slot that came back with a stale stack or a stale memory size consumes different gas
/// even when it happens to end with the same status.
struct Outcome
{
    evmc_status_code status = EVMC_INTERNAL_ERROR;
    int64_t gasLeft = 0;
    std::string output;
    int maxDepth = 0;

    bool operator==(Outcome const& _rhs) const
    {
        return status == _rhs.status && gasLeft == _rhs.gasLeft && output == _rhs.output;
    }
};

std::ostream& operator<<(std::ostream& _out, Outcome const& _o)
{
    return _out << "{status=" << _o.status << ", gasLeft=" << _o.gasLeft << ", output=" << _o.output
                << ", maxDepth=" << _o.maxDepth << '}';
}

/// A host whose CALL really re-enters the VM, so the pool is exercised at depth > 0. A mock that
/// answered CALL with a canned result would leave every frame at depth 0 and prove nothing.
class RecursiveHost : public evmc::MockedHost
{
public:
    RecursiveHost(evmc::VM& _vm, evmc::bytes _code) : m_vm(&_vm), m_code(std::move(_code)) {}

    evmc::Result call(evmc_message const& _msg) noexcept override
    {
        maxCallDepth = std::max(maxCallDepth, static_cast<int>(_msg.depth));
        return m_vm->execute(*this, kRev, _msg, m_code.data(), m_code.size());
    }

    int maxCallDepth = 0;

private:
    evmc::VM* m_vm;
    evmc::bytes m_code;
};

Outcome runOne(evmc::VM& _vm, evmc::bytes const& _code, evmc::bytes const& _input)
{
    RecursiveHost host{_vm, _code};
    host.accounts[kContract].nonce = 1;
    host.accounts[kContract].code = _code;

    evmc_message msg{};
    msg.kind = EVMC_CALL;
    msg.depth = 0;
    msg.gas = kGas;
    msg.recipient = kContract;
    msg.sender = kSender;
    msg.input_data = _input.data();
    msg.input_size = _input.size();

    auto result = _vm.execute(host, kRev, msg, _code.data(), _code.size());
    Outcome out;
    out.status = result.status_code;
    out.gasLeft = result.gas_left;
    out.output = toHex({result.output_data, result.output_size});
    out.maxDepth = host.maxCallDepth;
    return out;
}

// --- bytecode -------------------------------------------------------------------------------

/// RETURN_ONE: MSTORE(0, 1); RETURN(0, 32). Contains no JUMP and no JUMPDEST, so the historical
/// symptom cannot be produced by the program itself.
evmc::bytes const kReturnOne = fromHex("600160005260206000f3");

/// REVERT: REVERT(0, 0).
evmc::bytes const kRevert = fromHex("60006000fd");

/// BAD_JUMP: PUSH1 1; JUMP -- offset 1 is not a JUMPDEST.
evmc::bytes const kBadJump = fromHex("600156");

/// Self-recursive prologue shared by the three deep variants. CALLDATALOAD(0) is the remaining
/// depth; depth 0 jumps to the terminal at 0x27, depth n > 0 calls itself with n - 1. Frame `d`
/// occupies pool slot `d`, so running a shallow case afterwards re-enters slots a deep frame
/// last reset -- the reuse under test.
evmc::bytes const kDeepPrologue = fromHex(
    "6000358015602757"              // d; if d == 0 goto 0x27
    "6001900380600052"              // mem[0] = d - 1
    "60006000602060006000305af150"  // CALL(self, mem[0..32])
    "8060005260206000f3"            // return d - 1
    "5b");                          // 0x27: terminal starts at 40

/// Terminal returns 1; the whole chain succeeds.
evmc::bytes const kDeepOk = [](evmc::bytes _code) {
    auto tail = fromHex("600160005260206000f3");
    _code.insert(_code.end(), tail.begin(), tail.end());
    return _code;
}(kDeepPrologue);

/// Terminal reverts; every ancestor sees CALL fail and continues, so the outermost frame still
/// returns normally while a nested frame aborted via revert.
evmc::bytes const kDeepRevert = [](evmc::bytes _code) {
    auto tail = fromHex("60006000fd");
    _code.insert(_code.end(), tail.begin(), tail.end());
    return _code;
}(kDeepPrologue);

/// Terminal jumps back to itself (0x27) and burns gas until the frame is out of gas.
evmc::bytes const kDeepOutOfGas = [](evmc::bytes _code) {
    auto tail = fromHex("602756");
    _code.insert(_code.end(), tail.begin(), tail.end());
    return _code;
}(kDeepPrologue);
}  // namespace

BOOST_AUTO_TEST_SUITE(EvmoneVmReuseTest)

/// The premise: a deeply nested execution really does reach pool slots above 0. Without this the
/// differential test below would compare depth-0 executions and prove nothing.
BOOST_AUTO_TEST_CASE(DeepChainReachesNestedPoolSlots)
{
    auto& vm = threadLocalEvmoneVm();

    auto ok = runOne(vm, kDeepOk, word(16));
    BOOST_CHECK_EQUAL(ok.status, EVMC_SUCCESS);
    BOOST_CHECK_EQUAL(ok.maxDepth, 16);

    auto reverted = runOne(vm, kDeepRevert, word(9));
    BOOST_CHECK_EQUAL(reverted.status, EVMC_SUCCESS);  // the outermost frame still returns
    BOOST_CHECK_EQUAL(reverted.maxDepth, 9);

    auto exhausted = runOne(vm, kDeepOutOfGas, word(7));
    BOOST_CHECK_EQUAL(exhausted.status, EVMC_SUCCESS);
    BOOST_CHECK_EQUAL(exhausted.maxDepth, 7);
    // The terminal loops until the frame is out of gas, so the chain must have consumed nearly
    // everything. Without this, a terminal that silently returned would make the "aborted frame"
    // case indistinguishable from the clean one and the differential test weaker than it reads.
    BOOST_CHECK_LT(exhausted.gasLeft, kGas / 2);
    BOOST_CHECK_GT(ok.gasLeft, kGas / 2);
}

/// The load-bearing one: pollution does not change the answer.
///
/// One long-lived thread-local VM is driven through a sequence that grows the pool (depth 16),
/// aborts nested frames via out-of-gas and revert, and then re-enters shallow slots. Every step
/// is replayed on a VM that has never executed anything; equal status, gas left and output is the
/// statement threadLocalEvmoneVm relies on, and the statement VMInstance.cpp currently denies.
BOOST_AUTO_TEST_CASE(ReusedVmMatchesFreshVmAcrossFrameShapes)
{
    auto& reused = threadLocalEvmoneVm();

    struct Step
    {
        char const* name;
        evmc::bytes const* code;
        evmc::bytes input;
    };

    // Ordered so each case runs on a pool shaped by every case before it: deep before shallow,
    // aborted before clean, and deep(1) after deep(16) so slot 1 is entered having last been
    // reset by a frame of a different shape.
    std::vector<Step> const steps{
        {"deep-ok-16", &kDeepOk, word(16)},
        {"deep-oog-5", &kDeepOutOfGas, word(5)},
        {"return-one", &kReturnOne, {}},
        {"bad-jump", &kBadJump, {}},
        {"deep-revert-9", &kDeepRevert, word(9)},
        {"deep-ok-1", &kDeepOk, word(1)},
        {"revert", &kRevert, {}},
        {"return-one-again", &kReturnOne, {}},
        {"deep-ok-6", &kDeepOk, word(6)},
        {"deep-oog-2", &kDeepOutOfGas, word(2)},
        {"return-one-last", &kReturnOne, {}},
    };

    for (auto const& step : steps)
    {
        auto polluted = runOne(reused, *step.code, step.input);
        evmc::VM pristine{evmc_create_evmone()};
        auto fresh = runOne(pristine, *step.code, step.input);
        BOOST_CHECK_MESSAGE(
            polluted == fresh, step.name << ": reused " << polluted << " vs fresh " << fresh);
    }
}

/// The exact historical symptom, named so a regression reads clearly. After a deep run the pool's
/// shallow slots have been reset by many different frames; re-entering them with a contract that
/// contains no jump must still succeed.
BOOST_AUTO_TEST_CASE(ShallowExecutionAfterDeepChainIsNotBadJumpDestination)
{
    auto& vm = threadLocalEvmoneVm();
    BOOST_REQUIRE_EQUAL(runOne(vm, kDeepOk, word(24)).status, EVMC_SUCCESS);
    BOOST_REQUIRE_EQUAL(runOne(vm, kDeepOutOfGas, word(12)).status, EVMC_SUCCESS);

    auto out = runOne(vm, kReturnOne, {});
    BOOST_CHECK_EQUAL(out.status, EVMC_SUCCESS);
    BOOST_CHECK_NE(out.status, EVMC_BAD_JUMP_DESTINATION);
    BOOST_CHECK_EQUAL(out.output, toHex(word(1)));
}

BOOST_AUTO_TEST_SUITE_END()
