#include "../../../src/CallParameters.h"
#include "../../../src/executive/BlockContext.h"
#include "../../../src/executive/ExecutiveState.h"
#include "../../../src/vm/HostContext.h"
#include "../mock/MockExecutiveFactory.h"
#include "../mock/MockLedger.h"
#include <bcos-crypto/bcos-crypto/ChecksumAddress.h>

#include <boost/test/unit_test.hpp>
using namespace bcos;
using namespace bcos::executor;


namespace bcos
{
namespace test
{
namespace
{
bool memoryEqual(const evmc_address& a, const evmc_address& b)
{
    return std::equal(std::begin(a.bytes), std::end(a.bytes), std::begin(b.bytes));
}

/// A minimal SERIAL-lane executive: TransactionExecutive with the pure virtuals
/// stubbed, so the hook's serial default (keccak under feature_evm_address alone)
/// is reachable in-test.
class SerialMockTransactionExecutive : public bcos::executor::TransactionExecutive
{
public:
    SerialMockTransactionExecutive(const bcos::executor::BlockContext& blockContext,
        std::string contractAddress, int64_t contextID, int64_t seq)
      : TransactionExecutive(blockContext, std::move(contractAddress), contextID, seq)
    {}

    bcos::executor::CallParameters::UniquePtr start(
        bcos::executor::CallParameters::UniquePtr input) override
    {
        return input;
    }
    bcos::executor::CallParameters::UniquePtr externalCall(
        bcos::executor::CallParameters::UniquePtr input) override
    {
        return input;
    }
};
}  // namespace

struct ExecutiveStateFixture
{
    ExecutiveStateFixture()
    {
        ledgerCache = std::make_shared<LedgerCache>(std::make_shared<MockLedger>());
        auto input = std::make_unique<CallParameters>(CallParameters::Type::KEY_LOCK);
        input->staticCall = false;
        input->codeAddress = "aabbccddee";
        input->contextID = 1;
        input->seq = 1;
        blockContext =
            std::make_shared<BlockContext>(nullptr, ledgerCache, nullptr, 0, h256(), 0, 0, false);

        executiveFactory =
            std::make_shared<MockExecutiveFactory>(*blockContext, nullptr, nullptr, nullptr);
    }
    LedgerCache::Ptr ledgerCache;
    std::shared_ptr<MockTransactionExecutive> executive;
    std::shared_ptr<BlockContext> blockContext;
    std::shared_ptr<MockExecutiveFactory> executiveFactory;
    std::shared_ptr<ExecutiveState> executiveState;
    CallParameters::UniquePtr input;
};

BOOST_FIXTURE_TEST_SUITE(TestExecutiveState, ExecutiveStateFixture)

// [R5 F2] The v1 created-address warm-up sequence under feature_evm_eip2929:
// flag OFF reproduces the 0.21 metering exactly — the creation-time query (evmone
// 0.24's access_account on msg.recipient) answers cold WITHOUT inserting, so the
// first genuine touch pays the cold fee (2600) and only then the address turns warm;
// flag ON keeps 0.24/geth behaviour — warm from the creation query on. The cold/warm
// answers are the gas discriminators evmone meters 2600/100 from.
BOOST_AUTO_TEST_CASE(createdAddressWarmupSequenceFollowsFlag)
{
    evmc_address sender{};
    sender.bytes[19] = 0x2a;
    const uint64_t nonce = 7;
    auto hashImpl = std::make_shared<bcos::crypto::Keccak256>();
    bcos::executor::BlockContext localContext(
        nullptr, ledgerCache, hashImpl, 0, h256(), 0, 0, false);

    auto hostOf = [&](ledger::Features features, int64_t contextID) {
        auto exec = std::make_shared<MockTransactionExecutive>(localContext, "0x00", contextID, 41);
        localContext.setFeatures(features);
        auto params = std::make_unique<bcos::executor::CallParameters>(
            bcos::executor::CallParameters::Type::MESSAGE);
        params->staticCall = false;
        params->codeAddress = "aabbccddee";
        params->receiveAddress = "aabbccddee";
        params->contextID = 0;
        params->seq = 41;
        return bcos::executor::HostContext(std::move(params), exec, std::string("aabbccddee"));
    };

    ledger::Features base;
    base.set(ledger::Features::Flag::feature_evm_eip2929);
    base.set(ledger::Features::Flag::feature_evm_address);

    {  // flag OFF: base metering — cold (skip insert), cold (insert), warm.
        auto host = hostOf(base, 0);  // contextID 0: its own Eip2929AccessState
        auto created = host.createAddressForOpcode(sender, nonce);  // marks the address
        BOOST_CHECK_EQUAL(host.accessAccount(created, EVMC_BERLIN), EVMC_ACCESS_COLD);
        BOOST_CHECK_EQUAL(host.accessAccount(created, EVMC_BERLIN), EVMC_ACCESS_COLD);
        BOOST_CHECK_EQUAL(host.accessAccount(created, EVMC_BERLIN), EVMC_ACCESS_WARM);
    }

    {  // flag ON: 0.24/geth — warm from the creation query on.
        auto features = base;
        features.set(ledger::Features::Flag::bugfix_evm2929_created_address_warm);
        auto host = hostOf(features, 1);  // fresh state: the OFF case warmed contextID 0
        auto created = host.createAddressForOpcode(sender, nonce);
        BOOST_CHECK_EQUAL(host.accessAccount(created, EVMC_BERLIN), EVMC_ACCESS_COLD);
        BOOST_CHECK_EQUAL(host.accessAccount(created, EVMC_BERLIN), EVMC_ACCESS_WARM);
    }
}

// [R5 F1] The CREATE hook's EIP-style branch follows the scheduler's lane rule:
// SerialBlockExecutive honours keccak under feature_evm_address alone, DmcExecutor's
// arm additionally requires bugfix_dmc_create_address_rule — and the hook must agree
// with whichever lane it runs on, or the pushed address and the scheduler's empty-to
// derivation disagree. MockTransactionExecutive IS a CoroutineTransactionExecutive,
// so it exercises the DMC arm; SerialMockTransactionExecutive exercises the serial one.
BOOST_AUTO_TEST_CASE(createHookLaneGateFollowsSchedulerRule)
{
    evmc_address sender{};
    sender.bytes[19] = 0x2a;
    const uint64_t nonce = 7;
    // A local block context with a REAL hash impl: the fixture's has a null one, and
    // the hook's legacy branch hashes through blockContext().hashHandler().
    auto hashImpl = std::make_shared<bcos::crypto::Keccak256>();
    bcos::executor::BlockContext localContext(
        nullptr, ledgerCache, hashImpl, 0, h256(), 0, 0, false);

    auto hookOf = [&](std::shared_ptr<bcos::executor::TransactionExecutive> executive) {
        auto params = std::make_unique<bcos::executor::CallParameters>(
            bcos::executor::CallParameters::Type::MESSAGE);
        params->staticCall = false;
        params->codeAddress = "aabbccddee";
        params->receiveAddress = "aabbccddee";
        params->contextID = 0;
        params->seq = 41;
        bcos::executor::HostContext host(std::move(params), executive, std::string("aabbccddee"));
        return host.createAddressForOpcode(sender, nonce);
    };

    // The two derivations the gate chooses between, computed out-of-line: the lane's
    // legacy formula f(seq+1) and the EIP-style keccak(rlp(sender, nonce)).
    auto keccakOf = [&] {
        auto addrStr = bcos::newLegacyEVMAddressString(
            bcos::bytesConstRef{sender.bytes, sizeof(sender.bytes)}, bcos::u256(nonce));
        evmc_address out{};
        boost::algorithm::unhex(addrStr, out.bytes);
        return out;
    };
    auto formulaOf = [&](int64_t baseSeq) {
        auto addrStr = bcos::newEVMAddress(std::make_shared<bcos::crypto::Keccak256>(), 0, 0,
            baseSeq + 1);  // block 0, contextID 0 — the fixture's blockContext
        evmc_address out{};
        boost::algorithm::unhex(addrStr, out.bytes);
        return out;
    };

    // DMC lane (coroutine executive): feature_evm_address alone keeps the formula
    // address; adding bugfix_dmc_create_address_rule switches to keccak.
    {
        auto dmc = std::make_shared<MockTransactionExecutive>(localContext, "0x00", 0, 41);
        ledger::Features evmOnly;
        evmOnly.set(ledger::Features::Flag::feature_evm_address);
        localContext.setFeatures(evmOnly);
        BOOST_CHECK(memoryEqual(hookOf(dmc), formulaOf(41)));

        evmOnly.set(ledger::Features::Flag::bugfix_dmc_create_address_rule);
        localContext.setFeatures(evmOnly);
        BOOST_CHECK(memoryEqual(hookOf(dmc), keccakOf()));
    }

    // Serial lane: keccak under feature_evm_address alone — no bit-64 requirement,
    // because SerialBlockExecutive's own arm does not have one either.
    {
        auto serial = std::make_shared<SerialMockTransactionExecutive>(localContext, "0x00", 0, 41);
        ledger::Features evmOnly;
        evmOnly.set(ledger::Features::Flag::feature_evm_address);
        localContext.setFeatures(evmOnly);
        BOOST_CHECK(memoryEqual(hookOf(serial), keccakOf()));
    }
}

BOOST_AUTO_TEST_CASE(goTest)
{
    CallParameters::UniquePtr output;
    for (int8_t i = 0; i < 2; ++i)
    {
        EXECUTOR_LOG(DEBUG) << "goTest begin";
        if (i == 0)
        {
            EXECUTOR_LOG(DEBUG) << "i == 0 begin";
            auto callParameters = std::make_unique<CallParameters>(CallParameters::MESSAGE);
            callParameters->staticCall = false;
            callParameters->codeAddress = "aabbccddee";
            callParameters->contextID = i;
            callParameters->seq = i;
            auto executiveState =
                std::make_shared<ExecutiveState>(executiveFactory, std::move(callParameters));
            // EXECUTOR_LOG(DEBUG) << executiveState->getStatus();
            auto output = executiveState->go();
            BOOST_CHECK(executiveState->getStatus() == ExecutiveState::PAUSED);
            executiveState->setResumeParam(std::move(input));
            EXECUTOR_LOG(DEBUG) << "goTest: " << executiveState->getStatus();
            BOOST_CHECK(executiveState->getStatus() == ExecutiveState::NEED_RESUME);
            executiveState->go();
            EXECUTOR_LOG(DEBUG) << "i == 0  end!";
        }
        else if (i == 1)
        {
            EXECUTOR_LOG(DEBUG) << "i == 1 begin";
            auto callParameters = std::make_unique<CallParameters>(CallParameters::FINISHED);
            callParameters->staticCall = false;
            callParameters->codeAddress = "aabbccddee";
            callParameters->contextID = i;
            callParameters->seq = i;
            auto executiveState =
                std::make_shared<ExecutiveState>(executiveFactory, std::move(callParameters));
            EXECUTOR_LOG(DEBUG) << "goTest: " << executiveState->getStatus();
            executiveState->go();
            EXECUTOR_LOG(DEBUG) << "goTest: " << executiveState->getStatus();
            BOOST_CHECK(executiveState->getStatus() == ExecutiveState::FINISHED);
            // executiveState->go();
            EXECUTOR_LOG(DEBUG) << "goTest: " << executiveState->getStatus();
            EXECUTOR_LOG(DEBUG) << "i == 1  end!";
        }
    }
}

BOOST_AUTO_TEST_CASE(appendKeyLocksTest)
{
    EXECUTOR_LOG(DEBUG) << "appendKeyLocks begin";
    std::vector<std::string> keyLocks{"123", "134", "125"};
    for (int i = 0; i < 3; ++i)
    {
        if (i == 0)
        {
            auto callParameters = std::make_unique<CallParameters>(CallParameters::MESSAGE);
            callParameters->staticCall = false;
            callParameters->codeAddress = "aabbccddee";
            callParameters->contextID = i;
            callParameters->seq = i;
            callParameters->keyLocks = {"987"};
            auto executiveState =
                std::make_shared<ExecutiveState>(executiveFactory, std::move(callParameters));
            BOOST_CHECK(executiveState->getStatus() == ExecutiveState::NEED_RUN);
            executiveState->appendKeyLocks(keyLocks);
            EXECUTOR_LOG(DEBUG) << "i == 1 end ! status is :" << executiveState->getStatus();
        }
        else if (i == 1)
        {
            EXECUTOR_LOG(DEBUG) << "i == 2 begin";
            auto callParameters = std::make_unique<CallParameters>(CallParameters::MESSAGE);
            callParameters->staticCall = false;
            callParameters->codeAddress = "aabbccddee";
            callParameters->contextID = i;
            callParameters->seq = i;
            auto executiveState =
                std::make_shared<ExecutiveState>(executiveFactory, std::move(callParameters));
            executiveState->go();
            auto input1 = std::make_unique<CallParameters>(CallParameters::MESSAGE);
            input1->staticCall = false;
            input1->codeAddress = "aabbccddee";
            input1->contextID = i;
            input1->seq = i;
            input1->keyLocks = {"987"};
            executiveState->setResumeParam(std::move(input1));
            executiveState->appendKeyLocks(keyLocks);
            EXECUTOR_LOG(DEBUG) << "i == 2 end ! status is :" << executiveState->getStatus();
        }
        else
        {
            EXECUTOR_LOG(DEBUG) << "i == 3 begin";
            auto callParameters = std::make_unique<CallParameters>(CallParameters::REVERT);
            callParameters->staticCall = false;
            callParameters->codeAddress = "aabbccddee";
            callParameters->contextID = i;
            callParameters->seq = i;
            auto executiveState =
                std::make_shared<ExecutiveState>(executiveFactory, std::move(callParameters));
            executiveState->go();
            BOOST_CHECK(executiveState->getStatus() == ExecutiveState::FINISHED);
            executiveState->appendKeyLocks(keyLocks);
            EXECUTOR_LOG(DEBUG) << "i == 3 end ! status is :" << executiveState->getStatus();
        }
    }
}

// [R6 F2/F1] The DMC nested-CREATE derivation's scheduler hand-back contract:
// a stamped seq is adopted as the hook's derivation base, and an unstamped resume
// (-1 sentinel) must fail loudly instead of falling back to a frozen seq — the
// colliding-address shape the fix replaced. This pins the executor half of the fix
// (the seam in CoroutineTransactionExecutive) without the DMC through-flow harness.
BOOST_AUTO_TEST_CASE(createDerivationSeqAdoptsStampAndFailsLoudWithout)
{
    auto hashImpl = std::make_shared<bcos::crypto::Keccak256>();
    bcos::executor::BlockContext localContext(
        nullptr, ledgerCache, hashImpl, 0, h256(), 0, 0, false);

    auto stamped = std::make_shared<MockTransactionExecutive>(localContext, "0x00", 0, 41);
    stamped->adoptSchedulerSeqHandback(41);
    BOOST_CHECK_EQUAL(stamped->createDerivationSeq(), 41);

    auto unstamped = std::make_shared<MockTransactionExecutive>(localContext, "0x00", 1, 41);
    unstamped->adoptSchedulerSeqHandback(-1);
    BOOST_CHECK_THROW(unstamped->createDerivationSeq(), bcos::Error);
}


BOOST_AUTO_TEST_CASE(dmcNormalizationPreservesHostFaultAndHistoricalDelegateReverts)
{
    struct Normalizer : CoroutineTransactionExecutive
    {
        using CoroutineTransactionExecutive::CoroutineTransactionExecutive;
        using CoroutineTransactionExecutive::normalizeExternalCallResult;
    };
    auto hashImpl = std::make_shared<bcos::crypto::Keccak256>();
    BlockContext localContext(nullptr, ledgerCache, hashImpl, 0, h256(), 0,
        (uint32_t)protocol::BlockVersion::V3_18_0_VERSION, false);
    Normalizer executive(localContext, "0x00", 0, 0);
    for (bool fixNoAddress : {false, true})
    {
        ledger::Features features;
        if (fixNoAddress)
        {
            features.set(ledger::Features::Flag::bugfix_call_noaddr_return);
        }
        localContext.setFeatures(features);
        CallParameters fault(CallParameters::REVERT);
        fault.delegateCall = true;
        fault.evmStatus = EVMC_INTERNAL_ERROR;
        fault.status = (int32_t)protocol::TransactionStatus::RevertInstruction;
        executive.normalizeExternalCallResult(fault);
        BOOST_CHECK_EQUAL(fault.evmStatus, EVMC_INTERNAL_ERROR);
        BOOST_CHECK(fault.type == CallParameters::REVERT);
        CallParameters missingAddress(CallParameters::REVERT);
        missingAddress.delegateCall = true;
        missingAddress.evmStatus = EVMC_REVERT;
        missingAddress.status = (int32_t)protocol::TransactionStatus::CallAddressError;
        executive.normalizeExternalCallResult(missingAddress);
        BOOST_CHECK_EQUAL(missingAddress.evmStatus, fixNoAddress ? EVMC_SUCCESS : EVMC_REVERT);
    }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos
