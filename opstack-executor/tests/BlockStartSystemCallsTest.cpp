// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0

// BlockStartSystemCallsTest — runtime coverage for applyBlockStartSystemCalls
// (OpBlockExecute.h): the once-per-block EIP-4788 and EIP-2935 system calls actually run, their
// state diff lands in the block's storage, and the two ring buffers end up holding the values
// the EIPs specify.
//
// The other opstack tests run these calls against an EMPTY storage, where they are a no-op (no
// code at the system-contract addresses), so nothing so far exercised the ring buffers. Here
// the two predeploys are seeded with their CANONICAL deployed runtime bytecode, taken verbatim
// from the EIP texts, and the assertions read the resulting slots back:
//   EIP-4788 at 0x000F3df6D732807Ef1319fB7B8bB8522d0Beac02
//     slot[timestamp % 8191]        = timestamp
//     slot[timestamp % 8191 + 8191] = parent_beacon_block_root
//   EIP-2935 at 0x0000F90827F1C53A10CB7A02335B175320002935
//     slot[(number - 1) % 8191]     = parent block hash
// A wrong bytecode literal cannot produce a false pass: the contract would revert or write
// nothing, and the expected slot would read back as zero.
//
// WIRING NOTE: applyBlockStartSystemCalls is deliberately pipeline-agnostic and is not yet run
// per produced block. The block pipeline is being rewritten (OpScheduler), so the per-block
// call site is deferred until that lands; this file pins the unit so the wiring only has to
// choose where to call it.

#include <opstack-executor/OpBlockExecute.h>
#include <opstack-executor/OpstackExecutor.h>
#include <opstack-executor/RecentBlockHashes.h>
#include <opstack-executor/Storage2State.h>
#include <opstack-executor/Storage2StateHelpers.h>

#include <bcos-framework/storage/Entry.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/test/unit_test.hpp>
#include <evmc/evmc.hpp>

#include <bcos-evm/eth/state/state_diff.hpp>
#include <bcos-evm/eth/state/system_contracts.hpp>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

using bcos::executor_v1::StateKey;
using bcos::executor_v1::StateKeyView;
using bcos::executor_v1::StateValue;
namespace memory_storage = bcos::storage2::memory_storage;
namespace engine = bcos::evm::engine;
namespace op = bcos::evm::opstack;

namespace
{
// `bss*` name prefixes avoid an anonymous-namespace ODR clash with the other opstack test files.
using BssStorage = memory_storage::MemoryStorage<StateKey, StateValue,
    memory_storage::Attribute(memory_storage::ORDERED | memory_storage::LOGICAL_DELETION)>;

/// HISTORY_BUFFER_LENGTH (EIP-4788) == HISTORY_SERVE_WINDOW (EIP-2935).
constexpr uint64_t c_ringLength = 8191;

/// Deployed runtime bytecode of the beacon roots contract, verbatim from EIP-4788.
constexpr std::string_view c_beaconRootsCode =
    "3373fffffffffffffffffffffffffffffffffffffffe14604d57602036146024575f5ffd5b5f358015604957"
    "62001fff810690815414603c575f5ffd5b62001fff01545f5260205ff35b5f5ffd5b62001fff420642815"
    "55f359062001fff015500";
/// Deployed runtime bytecode of the history storage contract, verbatim from EIP-2935 (the
/// deployment prefix 0x60538060095f395ff3 of the EIP's init code is stripped: 0x53 == 83 bytes
/// of runtime code follow it).
constexpr std::string_view c_historyStorageCode =
    "3373fffffffffffffffffffffffffffffffffffffffe14604657602036036042575f356001430381116042"
    "57611fff81430311604257611fff9006545f5260205ff35b5f5ffd5b5f35611fff60014303065500";

evmc::bytes bssCode(std::string_view hex)
{
    auto const bytes = bcos::fromHex(hex);
    return {bytes.begin(), bytes.end()};
}

evmc::bytes32 bssWord(uint64_t value)
{
    evmc::bytes32 out{};
    for (size_t i = 0; i < sizeof(value); ++i)
    {
        out.bytes[sizeof(out.bytes) - 1 - i] = static_cast<uint8_t>(value >> (8 * i));
    }
    return out;
}

evmc::bytes32 bssMarkedHash(uint8_t marker)
{
    evmc::bytes32 out{};
    for (size_t i = 0; i < sizeof(out.bytes); ++i)
    {
        out.bytes[i] = static_cast<uint8_t>(marker + i);
    }
    return out;
}

/// Seed one account with code (nonce 0, balance 0), through the same bridge the production
/// write-back uses. seeding=true exempts the EIP-161 empty-account guard.
void bssSeedCode(BssStorage& storage, const evmc::address& addr, evmc::bytes code)
{
    evmone::state::StateDiff diff;
    evmone::state::StateDiff::Entry entry;
    entry.addr = addr;
    entry.nonce = 0;
    entry.balance = 0;
    entry.code = std::move(code);
    diff.modified_accounts.push_back(std::move(entry));

    bcos::evm::evmstate::Storage2State<BssStorage> bridge(storage);
    bridge.applyDiff(diff, /*seeding=*/true);
    BOOST_REQUIRE_MESSAGE(!bridge.poisoned(), "seed poisoned: " + std::string(bridge.firstError()));
}

/// Read one storage slot back out of the flat state (zero when the row is absent).
evmc::bytes32 bssReadSlot(BssStorage& storage, const evmc::address& addr, const evmc::bytes32& slot)
{
    auto entry = bcos::task::syncWait(bcos::storage2::readOne(storage,
        StateKeyView{bcos::evm::evmstate::accountTableName(addr),
            std::string_view{reinterpret_cast<const char*>(slot.bytes), sizeof(slot.bytes)}}));
    if (!entry.has_value())
    {
        return evmc::bytes32{};
    }
    auto const value = entry->get();
    BOOST_REQUIRE_EQUAL(value.size(), sizeof(evmc::bytes32::bytes));
    evmc::bytes32 out{};
    std::memcpy(out.bytes, value.data(), sizeof(out.bytes));
    return out;
}

std::string bssHex(const evmc::bytes32& value)
{
    return bcos::toHex(bcos::bytesConstRef(value.bytes, sizeof(value.bytes)));
}

struct BssFixture
{
    BssStorage storage;
    bcos::executor_v1::opstack::OpstackExecutor executor{nullptr, nullptr};

    static constexpr int64_t c_blockNumber = 1000;
    static constexpr uint64_t c_timestampSeconds = 1'700'000'123;

    evmc::bytes32 parentBeaconRoot = bssMarkedHash(0x10);
    evmc::bytes32 parentHash = bssMarkedHash(0x80);

    void run(evmc_revision rev)
    {
        evmone::state::BlockInfo blk{};
        blk.number = c_blockNumber;
        blk.timestamp = static_cast<int64_t>(c_timestampSeconds);
        blk.gas_limit = 30'000'000;
        blk.parent_beacon_block_root = parentBeaconRoot;

        std::optional<std::string> hashErr;
        engine::detail::RecentBlockHashes<BssStorage> hashes(
            storage, c_blockNumber, parentHash, &hashErr);

        bcos::evm::evmstate::Storage2State<BssStorage> stateView(storage, executor.sharedError());
        engine::applyBlockStartSystemCalls(stateView, blk, hashes, rev, executor.vm());
        BOOST_REQUIRE(!hashErr.has_value());
    }

    void seedBeaconRoots()
    {
        bssSeedCode(storage, evmone::state::BEACON_ROOTS_ADDRESS, bssCode(c_beaconRootsCode));
    }
    void seedHistoryStorage()
    {
        bssSeedCode(storage, evmone::state::HISTORY_STORAGE_ADDRESS, bssCode(c_historyStorageCode));
    }
};
}  // namespace

BOOST_AUTO_TEST_SUITE(BlockStartSystemCallsTest)

// EIP-4788: the beacon-roots contract keeps two ring buffers of HISTORY_BUFFER_LENGTH entries,
// the timestamp at `timestamp % 8191` and the root at `timestamp % 8191 + 8191`.
BOOST_AUTO_TEST_CASE(BeaconRootsRingBufferSlots)
{
    BssFixture f;
    f.seedBeaconRoots();
    f.run(EVMC_CANCUN);

    auto const index = BssFixture::c_timestampSeconds % c_ringLength;
    auto const timestampSlot =
        bssReadSlot(f.storage, evmone::state::BEACON_ROOTS_ADDRESS, bssWord(index));
    auto const rootSlot =
        bssReadSlot(f.storage, evmone::state::BEACON_ROOTS_ADDRESS, bssWord(index + c_ringLength));
    BOOST_CHECK_EQUAL(bssHex(timestampSlot), bssHex(bssWord(BssFixture::c_timestampSeconds)));
    BOOST_CHECK_EQUAL(bssHex(rootSlot), bssHex(f.parentBeaconRoot));
}

// EIP-2935: the history contract stores the PARENT block hash at `(number - 1) % 8191`.
// Isthmus maps to EVMC_PRAGUE, the revision the contract is registered from.
BOOST_AUTO_TEST_CASE(HistoryStorageRingBufferSlot)
{
    BssFixture f;
    f.seedHistoryStorage();
    f.run(EVMC_PRAGUE);

    auto const index = static_cast<uint64_t>(BssFixture::c_blockNumber - 1) % c_ringLength;
    auto const slot =
        bssReadSlot(f.storage, evmone::state::HISTORY_STORAGE_ADDRESS, bssWord(index));
    BOOST_CHECK_EQUAL(bssHex(slot), bssHex(f.parentHash));
}

// Both contracts seeded, one Prague call: each writes its own ring buffer, neither disturbs the
// other's.
BOOST_AUTO_TEST_CASE(PragueRunsBothContracts)
{
    BssFixture f;
    f.seedBeaconRoots();
    f.seedHistoryStorage();
    f.run(EVMC_PRAGUE);

    auto const beaconIndex = BssFixture::c_timestampSeconds % c_ringLength;
    BOOST_CHECK_EQUAL(bssHex(bssReadSlot(f.storage, evmone::state::BEACON_ROOTS_ADDRESS,
                          bssWord(beaconIndex + c_ringLength))),
        bssHex(f.parentBeaconRoot));
    auto const historyIndex = static_cast<uint64_t>(BssFixture::c_blockNumber - 1) % c_ringLength;
    BOOST_CHECK_EQUAL(bssHex(bssReadSlot(f.storage, evmone::state::HISTORY_STORAGE_ADDRESS,
                          bssWord(historyIndex))),
        bssHex(f.parentHash));
}

// Revision gating: EIP-2935 is registered from Prague, so a Cancun block must leave the history
// ring buffer untouched even with the contract deployed.
BOOST_AUTO_TEST_CASE(CancunDoesNotRunHistoryStorage)
{
    BssFixture f;
    f.seedBeaconRoots();
    f.seedHistoryStorage();
    f.run(EVMC_CANCUN);

    auto const historyIndex = static_cast<uint64_t>(BssFixture::c_blockNumber - 1) % c_ringLength;
    BOOST_CHECK_EQUAL(bssHex(bssReadSlot(f.storage, evmone::state::HISTORY_STORAGE_ADDRESS,
                          bssWord(historyIndex))),
        bssHex(evmc::bytes32{}));
}

// EIP-4788: "if no code exists at BEACON_ROOTS_ADDRESS, the call must fail silently". A chain
// whose genesis never allocated the predeploys therefore runs this step as a no-op rather than
// faulting — which is exactly why the pre-existing opstack tests, over an empty storage, never
// exercised the ring buffers above.
BOOST_AUTO_TEST_CASE(NoCodeAtPredeploysIsANoOp)
{
    BssFixture f;
    BOOST_CHECK_NO_THROW(f.run(EVMC_PRAGUE));
    auto const beaconIndex = BssFixture::c_timestampSeconds % c_ringLength;
    BOOST_CHECK_EQUAL(bssHex(bssReadSlot(f.storage, evmone::state::BEACON_ROOTS_ADDRESS,
                          bssWord(beaconIndex + c_ringLength))),
        bssHex(evmc::bytes32{}));
}

BOOST_AUTO_TEST_SUITE_END()
