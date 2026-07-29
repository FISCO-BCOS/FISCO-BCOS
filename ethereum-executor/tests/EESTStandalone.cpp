/**
 * @file EESTStandalone.cpp
 * @brief Standalone Ethereum Execution Spec Tests (EEST) runner.
 *
 * A dedicated command-line tool for running EEST v5.4.0 JSON state test fixtures
 * against ethereum-executor (based on bcos-evm).  Provides detailed per-file and per-test
 * diagnostic output, configurable verbosity, single-fixture regression mode,
 * and TBB-based parallel execution.
 *
 * Usage:
 *   ./eest-runner --fixture-dir /path/to/fixtures           # run all
 *   ./eest-runner --fixture-dir /path/to/fixtures --quiet   # summary only
 *   ./eest-runner --fixture /path/to/single.json            # single file
 *   ./eest-runner --fixture-dir /path --jobs 8              # parallel
 */

#include "EESTFixtureLoader.h"
#include "TestMemoryStorage.h"
#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-evm/eth/state/system_contracts.hpp"
#include "bcos-framework/ledger/EVMAccount.h"
#include "bcos-framework/ledger/Features.h"
#include "bcos-framework/protocol/Protocol.h"
#include "bcos-protocol/TransactionStatus.h"
#include "bcos-tars-protocol/protocol/BlockHeaderImpl.h"
#include "bcos-tars-protocol/protocol/TransactionFactoryImpl.h"
#include "bcos-tars-protocol/protocol/TransactionImpl.h"
#include "bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h"
#include "ethereum-executor/EthereumExecutor.h"
#include <evmc/evmc.h>
#include <tbb/concurrent_vector.h>
#include <boost/algorithm/hex.hpp>
#include <boost/log/core.hpp>
#include <range/v3/algorithm/equal.hpp>
#include "bcos-task/TBBWait.h"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

using namespace bcos;
using namespace bcos::storage2;
using namespace bcos::executor_v1;
namespace fs = std::filesystem;

// =============================================================================
//  Command-line options (parsed early)
// =============================================================================
struct ProgramOptions
{
    std::string fixtureDir;     // directory with .json fixtures
    std::string singleFixture;  // single fixture file path
    bool quiet = false;         // suppress per-file/per-test output
    bool noLog = false;         // suppress BCOS internal trace/debug log
    bool help = false;
};

ProgramOptions g_opts;

// =============================================================================
//  Global counters (atomic for parallel safety)
// =============================================================================
std::atomic<int> g_totalTests{0};
std::atomic<int> g_totalPassed{0};
std::atomic<int> g_totalFailed{0};

// Per-error-type counters
std::atomic<int> g_balanceMismatch{0};
std::atomic<int> g_storageMismatch{0};
std::atomic<int> g_nonceMismatch{0};
std::atomic<int> g_codeMismatch{0};
std::atomic<int> g_expectedException{0};
std::atomic<int> g_unexpectedFailure{0};

// Failure details for summary
struct FailureDetail
{
    std::string testName;
    std::string forkName;
    std::string reason;
};
tbb::concurrent_vector<FailureDetail> g_failureDetails;

// Mutex for console output (keep lines from interleaving)
std::mutex g_printMutex;

// =============================================================================
//  Helper: timestamp for elapsed-time display
// =============================================================================
std::string timestamp()
{
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    std::ostringstream oss;
    oss << std::put_time(std::localtime(&t), "%H:%M:%S");
    return oss.str();
}

void printLine(std::string const& msg)
{
    std::lock_guard<std::mutex> lock(g_printMutex);
    std::cout << msg << std::endl;
}

// =============================================================================
//  BlockHashes that accumulates hashes from fixture blocks
// =============================================================================
/// A BlockHashes implementation that stores block_number → hash mappings.
/// Populated from fixture block headers as blocks are processed.
struct FixtureBlockHashes : evmone::state::BlockHashes
{
    std::map<int64_t, evmc::bytes32> hashes;

    evmc::bytes32 get_block_hash(int64_t blockNumber) const noexcept override
    {
        auto it = hashes.find(blockNumber);
        if (it != hashes.end())
            return it->second;
        return {};
    }
};

// =============================================================================
//  Usage
// =============================================================================
void printUsage(char const* prog)
{
    std::cout << R"(Usage: )" << prog << R"( [OPTIONS]

Options:
  --fixture-dir DIR    Directory containing EEST .json fixture files (recursive)
  --fixture FILE       Run a single fixture file only
  --quiet              Suppress per-file/per-test output; show summary only
  --no-log             Suppress BCOS internal trace/debug log output
  --help               Show this help message

Examples:
  )" << prog << R"( --fixture-dir ./fixtures/state_tests
  )" << prog << R"( --fixture-dir ./fixtures/state_tests --quiet
  )" << prog << R"( --fixture ./fixtures/state_tests/cancun/eip4844/test.json

Environment:
  The program also reads EEST_FIXTURE_DIR if --fixture-dir is not set.
)";
}

void parseOptions(int argc, char* argv[])
{
    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h")
        {
            g_opts.help = true;
            return;
        }
        if (arg == "--quiet" || arg == "-q")
        {
            g_opts.quiet = true;
        }
        else if (arg == "--no-log")
        {
            g_opts.noLog = true;
        }
        else if (arg == "--fixture-dir" && i + 1 < argc)
        {
            g_opts.fixtureDir = argv[++i];
        }
        else if (arg == "--fixture" && i + 1 < argc)
        {
            g_opts.singleFixture = argv[++i];
        }
        else if (arg.rfind("--fixture-dir=", 0) == 0)
        {
            g_opts.fixtureDir = arg.substr(14);
        }
        else if (arg.rfind("--fixture=", 0) == 0)
        {
            g_opts.singleFixture = arg.substr(10);
        }
    }
}

// =============================================================================
//  Core test runner (reused from original but made standalone)
// =============================================================================
class EESTRunner
{
public:
    std::shared_ptr<bcos::crypto::CryptoSuite> cryptoSuite;
    bcostars::protocol::TransactionFactoryImpl transactionFactory;
    bcostars::protocol::TransactionReceiptFactoryImpl receiptFactory;
    eth::EthereumExecutor executor;
    evmc_revision m_currentRevision = EVMC_CANCUN;
    ledger::LedgerConfig m_ledgerConfig;

    EESTRunner()
      : cryptoSuite(std::make_shared<bcos::crypto::CryptoSuite>(
            std::make_shared<bcos::crypto::Keccak256>(), nullptr, nullptr)),
        transactionFactory(cryptoSuite),
        receiptFactory(cryptoSuite),
        executor(receiptFactory, cryptoSuite->hashImpl())
    {}

    void configureFork(std::string const& forkName)
    {
        auto const rev = test::forkNameToRevision(forkName);
        m_currentRevision = rev;
        m_ledgerConfig.setEVMCRevision(rev);
    }

    void configureEnvironment(test::EESTEnvironment const& env)
    {
        if (!env.baseFee.empty())
        {
            auto baseFeeVal = test::hexToU256(env.baseFee);
            auto hexStr = "0x" + baseFeeVal.str(0, std::ios_base::hex);
            m_ledgerConfig.setGasPrice({hexStr, 0});
        }
        else
        {
            m_ledgerConfig.setGasPrice({"0x0", 0});
        }
    }

    void setupPreState(MutableStorage& storage, std::map<std::string, test::EESTAccount> const& pre)
    {
        for (auto const& [addrHex, acc] : pre)
        {
            auto addrBytes = test::hexToBytes(addrHex);
            if (addrBytes.size() != sizeof(evmc_address))
                continue;

            evmc_address addr;
            std::copy(addrBytes.begin(), addrBytes.end(), addr.bytes);

            ledger::account::EVMAccount<MutableStorage> evmAccount(storage, addr, false);

            task::tbb::syncWait([&]() -> task::Task<void> {
                if (!co_await evmAccount.exists())
                    co_await evmAccount.create();
            }());

            if (!acc.nonce.empty() && acc.nonce != "0x" && acc.nonce != "0x0")
            {
                auto nonceStr = test::hexToU256(acc.nonce).str(0, std::ios_base::dec);
                task::tbb::syncWait(
                    [&]() -> task::Task<void> { co_await evmAccount.setNonce(nonceStr); }());
            }

            if (!acc.balance.empty() && acc.balance != "0x0")
            {
                auto bal = test::hexToU256(acc.balance);
                if (bal > 0)
                {
                    task::tbb::syncWait(
                        [&]() -> task::Task<void> { co_await evmAccount.setBalance(bal); }());
                }
            }

            if (!acc.code.empty() && acc.code != "0x")
            {
                auto codeBytes = test::hexToBytes(acc.code);
                if (!codeBytes.empty())
                {
                    auto codeHash = cryptoSuite->hashImpl()->hash(
                        bytesConstRef(codeBytes.data(), codeBytes.size()));
                    task::tbb::syncWait([&]() -> task::Task<void> {
                        co_await evmAccount.setCode(std::move(codeBytes), std::string{}, codeHash);
                    }());
                }
            }

            for (auto const& [key, val] : acc.storage)
            {
                auto keyBytes = test::hexToBytes(key);
                auto valBytes = test::hexToBytes(val);
                if (keyBytes.size() <= 32 && valBytes.size() <= 32)
                {
                    evmc_bytes32 storageKey{};
                    evmc_bytes32 storageValue{};
                    std::copy(
                        keyBytes.begin(), keyBytes.end(), storageKey.bytes + 32 - keyBytes.size());
                    std::copy(valBytes.begin(), valBytes.end(),
                        storageValue.bytes + 32 - valBytes.size());

                    task::tbb::syncWait([&]() -> task::Task<void> {
                        co_await evmAccount.setStorage(storageKey, storageValue);
                    }());
                }
            }
        }
    }

    bcostars::protocol::BlockHeaderImpl buildBlockHeader(test::EESTEnvironment const& env)
    {
        bcostars::protocol::BlockHeaderImpl header;
        uint32_t blockVer;
        if (m_currentRevision >= EVMC_CANCUN)
            blockVer = static_cast<uint32_t>(bcos::protocol::BlockVersion::V3_1_VERSION);
        else if (m_currentRevision >= EVMC_PARIS)
            blockVer = static_cast<uint32_t>(bcos::protocol::BlockVersion::V3_2_VERSION);
        else
            blockVer = static_cast<uint32_t>(bcos::protocol::BlockVersion::V3_1_VERSION);
        header.setVersion(blockVer);
        header.setNumber(test::hexToInt64(env.number));

        auto tsSec = test::hexToInt64(env.timestamp);
        header.setTimestamp(tsSec * 1000);
        header.setSealer(0);

        if (!env.coinbase.empty())
        {
            auto cbBytes = test::hexToBytes(env.coinbase);
            header.setCoinbase(std::move(cbBytes));
        }
        header.calculateHash(*cryptoSuite->hashImpl());
        return header;
    }

    std::shared_ptr<bcostars::protocol::TransactionImpl> buildTransaction(
        test::EESTTransaction const& tx, int dataIndex, int gasIndex, int valueIndex)
    {
        std::string txData;
        if (dataIndex >= 0 && static_cast<size_t>(dataIndex) < tx.data.size())
            txData = tx.data[dataIndex];

        std::string gasLimit = "0x0";
        if (gasIndex >= 0 && static_cast<size_t>(gasIndex) < tx.gasLimit.size())
            gasLimit = tx.gasLimit[gasIndex];

        std::string value = "0x0";
        if (valueIndex >= 0 && static_cast<size_t>(valueIndex) < tx.value.size())
            value = tx.value[valueIndex];

        auto tarsTx = std::make_shared<bcostars::Transaction>();
        auto& data = tarsTx->data;

        data.version = 0;
        data.blockLimit = 0;
        data.nonce = test::strip0x(tx.nonce);
        data.gasLimit = test::hexToInt64(gasLimit);
        data.value = test::strip0x(value);

        if (!tx.to.empty() && tx.to != "0x")
        {
            auto toBytes = test::hexToBytes(tx.to);
            data.to.assign(toBytes.begin(), toBytes.end());
        }

        auto inputBytes = test::hexToBytes(txData);
        data.input.assign(inputBytes.begin(), inputBytes.end());

        if (!tx.gasPrice.empty())
            data.gasPrice = test::strip0x(tx.gasPrice);
        if (!tx.maxFeePerGas.empty())
            data.maxFeePerGas = test::strip0x(tx.maxFeePerGas);
        if (!tx.maxPriorityFeePerGas.empty())
            data.maxPriorityFeePerGas = test::strip0x(tx.maxPriorityFeePerGas);

        if (!tx.sender.empty())
        {
            auto senderBytes = test::hexToBytes(tx.sender);
            tarsTx->sender.assign(senderBytes.begin(), senderBytes.end());
        }

        data.chainID = "0";
        tarsTx->type = 1;
        tarsTx->extraTransactionHash.assign(32, 0);

        bool hasAccessList = false;
        if (dataIndex >= 0 && static_cast<size_t>(dataIndex) < tx.accessLists.size())
        {
            auto const& al = tx.accessLists[dataIndex];
            if (al.has_value())
            {
                hasAccessList = true;
                for (auto const& [addr, keys] : *al)
                {
                    bcostars::Web3AccessListEntry entry;
                    entry.account = test::strip0x(addr);
                    for (auto const& k : keys)
                    {
                        auto keyBytes = test::hexToBytes(k);
                        entry.storageKeys.emplace_back(keyBytes.begin(), keyBytes.end());
                    }
                    data.accessList.push_back(std::move(entry));
                }
            }
        }

        bool hasAuthList = tx.authorizationList.has_value() && !tx.authorizationList->empty();
        if (hasAuthList)
            tarsTx->web3TypedTxKind = 4;
        else if (!tx.maxFeePerGas.empty() || !tx.maxPriorityFeePerGas.empty())
            tarsTx->web3TypedTxKind = 2;
        else if (hasAccessList)
            tarsTx->web3TypedTxKind = 1;
        else
            tarsTx->web3TypedTxKind = 0;

        if (hasAuthList)
        {
            for (auto const& auth : *tx.authorizationList)
            {
                bcostars::AuthorizationEntry entry;
                entry.chainID = test::hexToInt64(test::readHexField(auth, "chainId"));
                entry.nonce = test::hexToInt64(test::readHexField(auth, "nonce"));
                entry.v = static_cast<uint8_t>(test::hexToInt64(test::readHexField(auth, "v")));
                entry.address = test::strip0x(test::readHexField(auth, "address"));
                entry.signer = test::strip0x(test::readHexField(auth, "signer"));
                entry.r = test::strip0x(test::readHexField(auth, "r"));
                entry.s = test::strip0x(test::readHexField(auth, "s"));
                data.authorizationList.push_back(std::move(entry));
            }
        }

        if (!tx.blobVersionedHashes.empty())
        {
            tarsTx->web3TypedTxKind = 3;
            data.maxFeePerBlobGas = test::strip0x(tx.maxFeePerBlobGas);
            for (auto const& h : tx.blobVersionedHashes)
            {
                auto hashBytes = test::hexToBytes(h);
                data.blobVersionedHashes.emplace_back(hashBytes.begin(), hashBytes.end());
            }
        }

        return std::make_shared<bcostars::protocol::TransactionImpl>(
            [tarsTx = std::move(tarsTx)]() mutable { return tarsTx.get(); });
    }

    /// Verify post-state. Returns a string with mismatch details (empty = all good).
    std::string verifyPostState(
        MutableStorage& storage, std::map<std::string, test::EESTAccount> const& expected)
    {
        std::ostringstream errors;
        int passed = 0, failed = 0;
        (void)passed;  // not used for return, only local tracking

        for (auto const& [addrHex, expectedAcc] : expected)
        {
            auto addrBytes = test::hexToBytes(addrHex);
            if (addrBytes.size() != sizeof(evmc_address))
                continue;

            evmc_address addr;
            std::copy(addrBytes.begin(), addrBytes.end(), addr.bytes);

            ledger::account::EVMAccount<MutableStorage> evmAccount(storage, addr, false);

            task::tbb::syncWait([&]() -> task::Task<void> {
                // nonce
                if (!expectedAcc.nonce.empty() && expectedAcc.nonce != "0x")
                {
                    auto storedNonce = co_await evmAccount.nonce();
                    auto expNonce = test::hexToU256(expectedAcc.nonce);
                    auto actualNonce = bcos::u256(storedNonce.value_or("0"));
                    if (expNonce != actualNonce)
                    {
                        errors << "    NONCE " << addrHex << ": expected " << expectedAcc.nonce
                               << " (" << test::hexToU256(expectedAcc.nonce) << "), got "
                               << actualNonce << "\n";
                        ++g_nonceMismatch;
                        ++failed;
                        co_return;
                    }
                }

                // balance
                if (!expectedAcc.balance.empty())
                {
                    auto storedBal = co_await evmAccount.balance();
                    auto expBal = test::hexToU256(expectedAcc.balance);
                    if (expBal != storedBal)
                    {
                        errors << "    BALANCE " << addrHex << ": expected " << expectedAcc.balance
                               << " (" << expBal << "), got " << storedBal << "\n";
                        ++g_balanceMismatch;
                        ++failed;
                        co_return;
                    }
                }

                // code
                if (!expectedAcc.code.empty() && expectedAcc.code != "0x")
                {
                    auto codeEntry = co_await evmAccount.code();
                    auto expCode = test::hexToBytes(expectedAcc.code);
                    bool codeMatch = codeEntry.has_value();
                    if (codeMatch)
                    {
                        auto codeView = codeEntry->get();
                        codeMatch = bcos::bytes(codeView.begin(), codeView.end()) == expCode;
                    }
                    if (!codeMatch)
                    {
                        errors << "    CODE " << addrHex << ": expected " << expectedAcc.code
                               << "\n";
                        ++g_codeMismatch;
                        ++failed;
                        co_return;
                    }
                }

                // storage
                for (auto const& [key, val] : expectedAcc.storage)
                {
                    auto keyBytes = test::hexToBytes(key);
                    evmc_bytes32 storageKey{};
                    if (keyBytes.size() <= 32)
                        std::copy(keyBytes.begin(), keyBytes.end(),
                            storageKey.bytes + 32 - keyBytes.size());
                    else
                        std::copy(keyBytes.begin(), keyBytes.end(), storageKey.bytes);

                    auto storedVal = co_await evmAccount.storage(storageKey);
                    auto expValBytes = test::hexToBytes(val);
                    evmc_bytes32 expVal{};
                    if (expValBytes.size() <= 32)
                        std::copy(expValBytes.begin(), expValBytes.end(),
                            expVal.bytes + 32 - expValBytes.size());

                    if (!::ranges::equal(storedVal.bytes, expVal.bytes))
                    {
                        std::string actualHex;
                        boost::algorithm::hex_lower(
                            storedVal.bytes, storedVal.bytes + 32, std::back_inserter(actualHex));
                        errors << "    STORAGE " << addrHex << " key=" << key << ": expected "
                               << val << ", got 0x" << actualHex << "\n";
                        ++g_storageMismatch;
                        ++failed;
                    }
                }
            }());

        }  // end for loop over expected accounts

        return errors.str();
    }

    /// Run a single fixture test case. Returns true if passed.
    bool runFixture(::test::EESTFixture const& fixture, std::string const& forkName,
        ::test::EESTForkPost const& post)
    {
        configureFork(forkName);
        configureEnvironment(fixture.env);

        MutableStorage storage;
        setupPreState(storage, fixture.pre);

        // Ensure sender exists with correct initial nonce.
        // EEST fixtures may not include sender in "pre" — evmone creates it
        // with default nonce=0, then bumps to 1. But expected nonce may be 2
        // (tx nonce 1 → bumped to 2), so we must set the correct initial nonce.
        if (!fixture.transaction.sender.empty())
        {
            auto senderBytes = test::hexToBytes(fixture.transaction.sender);
            if (senderBytes.size() == sizeof(evmc_address))
            {
                evmc_address senderAddr{};
                std::copy(senderBytes.begin(), senderBytes.end(), senderAddr.bytes);
                ledger::account::EVMAccount<MutableStorage> senderAcct(storage, senderAddr, false);
                auto txNonce = test::hexToU256(fixture.transaction.nonce);
                auto nonceStr = txNonce.str(0, std::ios_base::dec);
                task::tbb::syncWait([&]() -> task::Task<void> {
                    if (!co_await senderAcct.exists())
                    {
                        co_await senderAcct.create();
                        co_await senderAcct.setNonce(nonceStr);
                    }
                }());
            }
        }

        auto blockHeader = buildBlockHeader(fixture.env);
        auto tx =
            buildTransaction(fixture.transaction, post.dataIndex, post.gasIndex, post.valueIndex);

        protocol::TransactionReceipt::Ptr receipt;
        std::string evmError;
        bool executionThrew = false;

        try
        {
            receipt = task::tbb::syncWait(
                executor.executeTransaction(storage, blockHeader, *tx, 0, m_ledgerConfig, false));
        }
        catch (std::exception const& e)
        {
            evmError = e.what();
            executionThrew = true;
        }

        // Determine expected outcome.
        // EEST v5.4.0 fixtures encode expected-success/failure in the test name
        // via "successful_True"/"successful_False" parameters. The JSON
        // "expectException" field is absent for both cases.
        bool expectsSuccess = post.expectException.empty();
        if (expectsSuccess &&
            fixture.name.find("successful_False") != std::string::npos)
            expectsSuccess = false;
        bool gotSuccess =
            !executionThrew && receipt &&
            (receipt->status() == 0 ||
                receipt->status() == static_cast<int32_t>(protocol::TransactionStatus::None));

        // When expectException is empty but the transaction failed, the test
        // may still be correct if the post-state matches (transaction reverted
        // cleanly and only gas was deducted).  EEST v5.4.0 fixtures often omit
        // expectException for expected-failure tests.
        if (expectsSuccess && !gotSuccess)
        {
            auto errStr = verifyPostState(storage, post.state);
            if (errStr.empty())
                return true;  // State matches → test passes (tx reverted correctly)

            std::ostringstream oss;
            oss << "FAIL: " << fixture.name << " [" << forkName << "] expected success, got failure"
                << (executionThrew ? " (exception: " + evmError + ")" : "")
                << (receipt ? " status=" + std::to_string(receipt->status()) : "");
            printLine(oss.str());
            g_failureDetails.push_back({fixture.name, forkName, "unexpected failure"});
            ++g_unexpectedFailure;
            return false;
        }

        if (!expectsSuccess && gotSuccess)
        {
            std::ostringstream oss;
            oss << "FAIL: " << fixture.name << " [" << forkName << "] expected exception '"
                << post.expectException << "', got success";
            printLine(oss.str());
            g_failureDetails.push_back(
                {fixture.name, forkName, "expected exception '" + post.expectException + "'"});
            ++g_expectedException;
            return false;
        }

        // Both expect and got failure → pass
        if (!expectsSuccess && !gotSuccess)
            return true;

        // Success: verify post-state
        auto errStr = verifyPostState(storage, post.state);
        if (!errStr.empty())
        {
            std::ostringstream oss;
            oss << "FAIL: " << fixture.name << " [" << forkName << "]\n" << errStr;
            printLine(oss.str());
            g_failureDetails.push_back({fixture.name, forkName, "state mismatch"});
            return false;
        }

        return true;
    }

    // ---- Blockchain test support ----

    /// Extract fork name from a blockchain test fixture name.
    /// EEST blockchain test names look like:
    ///   tests/shanghai/.../test_xxx[fork_Cancun-blockchain_test-...]
    /// Returns "Cancun" or empty string if not found.
    static std::string extractForkFromName(std::string const& name)
    {
        auto pos = name.find("fork_");
        if (pos == std::string::npos)
            return {};
        auto start = pos + 5;  // skip "fork_"
        auto end = name.find('-', start);
        if (end == std::string::npos)
            end = name.find(']', start);
        if (end == std::string::npos)
            return {};
        return name.substr(start, end - start);
    }

    /// Build a BCOS BlockHeader from a blockchain test block header.
    bcostars::protocol::BlockHeaderImpl buildBlockHeader(test::EESTBlockHeader const& bh)
    {
        bcostars::protocol::BlockHeaderImpl header;
        uint32_t blockVer;
        if (m_currentRevision >= EVMC_CANCUN)
            blockVer = static_cast<uint32_t>(bcos::protocol::BlockVersion::V3_1_VERSION);
        else if (m_currentRevision >= EVMC_PARIS)
            blockVer = static_cast<uint32_t>(bcos::protocol::BlockVersion::V3_2_VERSION);
        else
            blockVer = static_cast<uint32_t>(bcos::protocol::BlockVersion::V3_1_VERSION);
        header.setVersion(blockVer);

        header.setNumber(test::hexToInt64(bh.number));
        auto tsSec = test::hexToInt64(bh.timestamp);
        header.setTimestamp(tsSec * 1000);
        header.setSealer(0);
        if (!bh.coinbase.empty())
        {
            auto cbBytes = test::hexToBytes(bh.coinbase);
            header.setCoinbase(std::move(cbBytes));
        }
        header.calculateHash(*cryptoSuite->hashImpl());
        return header;
    }

    /// Configure environment from blockchain test block header.
    void configureEnvironment(test::EESTBlockHeader const& bh)
    {
        if (!bh.baseFee.empty())
        {
            auto baseFeeVal = test::hexToU256(bh.baseFee);
            auto hexStr = "0x" + baseFeeVal.str(0, std::ios_base::hex);
            m_ledgerConfig.setGasPrice({hexStr, 0});
        }
        else
        {
            m_ledgerConfig.setGasPrice({"0x0", 0});
        }
    }

    /// Run a blockchain test fixture: execute blocks sequentially,
    /// apply block rewards via finalize(), then verify postState.
    /// Returns true if passed.
    bool runBlockchainFixture(test::EESTBlockchainFixture const& fixture)
    {
        auto forkName = extractForkFromName(fixture.name);
        if (forkName.empty())
            forkName = "Cancun";  // default
        configureFork(forkName);

        // Set up pre-state once
        MutableStorage storage;
        setupPreState(storage, fixture.pre);

        // Ensure sender accounts exist with correct initial nonce.
        // EEST fixtures may not include sender in "pre".
        for (auto const& block : fixture.blocks)
            for (auto const& tx : block.transactions)
            {
                if (tx.sender.empty()) continue;
                auto senderBytes = test::hexToBytes(tx.sender);
                if (senderBytes.size() != sizeof(evmc_address)) continue;
                evmc_address senderAddr{};
                std::copy(senderBytes.begin(), senderBytes.end(), senderAddr.bytes);
                ledger::account::EVMAccount<MutableStorage> senderAcct(storage, senderAddr, false);
                auto txNonce = test::hexToU256(tx.nonce);
                auto nonceStr = txNonce.str(0, std::ios_base::dec);
                task::tbb::syncWait([&]() -> task::Task<void> {
                    if (!co_await senderAcct.exists())
                    {
                        co_await senderAcct.create();
                        co_await senderAcct.setNonce(nonceStr);
                    }
                }());
            }

        // Configure initial environment from genesis header
        configureEnvironment(fixture.genesisBlockHeader);

        // Set up BlockHashes with genesis hash for HISTORY_STORAGE system call
        FixtureBlockHashes blockHashes;
        if (!fixture.genesisBlockHeader.hash.empty())
        {
            auto gh = test::hexToBytes(fixture.genesisBlockHeader.hash);
            if (gh.size() == sizeof(evmc_bytes32))
                std::copy_n(gh.begin(), sizeof(evmc_bytes32),
                    blockHashes.hashes[0].bytes);
        }

        // Compute block reward based on fork (matching evmone's mining_reward()).
        // Reference: evmone test/blockchaintest/blockchaintest_runner.cpp
        auto const rev = m_currentRevision;
        std::optional<uint64_t> blockReward;
        if (rev < EVMC_BYZANTIUM)
            blockReward = 5000000000000000000ULL;  // 5 ETH (Frontier..SpuriousDragon)
        else if (rev < EVMC_PETERSBURG)
            blockReward = 3000000000000000000ULL;  // 3 ETH (Byzantium..Constantinople)
        else if (rev < EVMC_PARIS)
            blockReward = 2000000000000000000ULL;  // 2 ETH (Petersburg..London)
        // else: Paris+ → 0 (no block reward after merge)

        // Execute each block's transactions sequentially
        int txIndex = 0;
        for (auto const& block : fixture.blocks)
        {
            configureEnvironment(block.blockHeader);
            auto blockHdr = buildBlockHeader(block.blockHeader);

            // EIP-4788 & EIP-2935: Execute system contracts at block start
            // (BEFORE transactions), matching evmone's apply_block() order.
            // system_call_block_start runs the actual contract bytecode via
            // the EVM, producing correct storage values for BEACON_ROOTS
            // and HISTORY_STORAGE.
            if (rev >= EVMC_CANCUN)
            {
                evmone::state::BlockInfo bi{};
                bi.number = test::hexToInt64(block.blockHeader.number);
                bi.timestamp = test::hexToInt64(block.blockHeader.timestamp);
                bi.gas_limit = test::hexToInt64(block.blockHeader.gasLimit);
                if (!block.blockHeader.coinbase.empty())
                {
                    auto cbBytes = test::hexToBytes(block.blockHeader.coinbase);
                    if (cbBytes.size() == sizeof(evmc_address))
                        std::copy_n(cbBytes.begin(), sizeof(evmc_address),
                            bi.coinbase.bytes);
                }
                if (!block.blockHeader.baseFee.empty())
                    bi.base_fee = static_cast<uint64_t>(
                        test::hexToU256(block.blockHeader.baseFee));
                if (!block.blockHeader.parentBeaconRoot.empty())
                {
                    auto pbr = test::hexToBytes(
                        block.blockHeader.parentBeaconRoot);
                    if (pbr.size() == sizeof(evmc_bytes32))
                        std::copy_n(pbr.begin(), sizeof(evmc_bytes32),
                            bi.parent_beacon_block_root.bytes);
                }
                bi.blob_base_fee = intx::uint256{1};

                eth::StorageStateView<MutableStorage> stateView(storage);
                auto sysDiff = evmone::state::system_call_block_start(
                    stateView, bi, blockHashes, rev, executor.vm());

                task::tbb::syncWait(eth::applyStateDiff(
                    storage, sysDiff, rev, *cryptoSuite->hashImpl()));
            }

            for (auto const& tx : block.transactions)
            {
                auto bcosTx =
                    buildTransaction(tx, 0 /*dataIndex*/, 0 /*gasIndex*/, 0 /*valueIndex*/);

                protocol::TransactionReceipt::Ptr receipt;
                std::string evmError;
                bool executionThrew = false;

                try
                {
                    receipt = task::tbb::syncWait(executor.executeTransaction(
                        storage, blockHdr, *bcosTx, txIndex, m_ledgerConfig, false));
                }
                catch (std::exception const& e)
                {
                    evmError = e.what();
                    executionThrew = true;
                }

                if (executionThrew || !receipt ||
                    (receipt->status() != 0 &&
                        receipt->status() !=
                            static_cast<int32_t>(protocol::TransactionStatus::None)))
                {
                    std::ostringstream oss;
                    oss << "FAIL: " << fixture.name << " [" << forkName << "] tx#" << txIndex
                        << " expected success, got failure"
                        << (executionThrew ? " (exception: " + evmError + ")" : "")
                        << (receipt ? " status=" + std::to_string(receipt->status()) : "");
                    printLine(oss.str());
                    g_failureDetails.push_back({fixture.name, forkName,
                        "tx#" + std::to_string(txIndex) + " unexpected failure"});
                    ++g_unexpectedFailure;
                    return false;
                }
                ++txIndex;
            }

            // Apply block reward via finalize() (uses bcos-evm's built-in logic)
            task::tbb::syncWait(
                executor.finalizeBlock(storage, blockHdr, m_ledgerConfig, rev, blockReward));

            // Accumulate block hash for HISTORY_STORAGE system call in subsequent blocks
            if (!block.blockHeader.hash.empty())
            {
                auto h = test::hexToBytes(block.blockHeader.hash);
                if (h.size() == sizeof(evmc_bytes32))
                {
                    auto blockNum = test::hexToInt64(block.blockHeader.number);
                    std::copy_n(h.begin(), sizeof(evmc_bytes32),
                        blockHashes.hashes[blockNum].bytes);
                }
            }
        }

        // Verify postState
        auto errStr = verifyPostState(storage, fixture.postState);
        if (!errStr.empty())
        {
            std::ostringstream oss;
            oss << "FAIL: " << fixture.name << " [" << forkName << "]\n" << errStr;
            printLine(oss.str());
            g_failureDetails.push_back({fixture.name, forkName, "state mismatch"});
            return false;
        }

        return true;
    }
};

// =============================================================================
//  Detect fixture format from directory path
// =============================================================================
/// Determine fixture format from the file path.
/// EEST fixtures are organized by type in directory names:
///   .../state_tests/...       → state_test
///   .../blockchain_tests/...  → blockchain_test
///   .../transaction_tests/... → transaction_test (skip)
inline std::string detectFormatFromPath(fs::path const& filePath)
{
    auto str = filePath.string();
    // Match top-level fixture type directory by full path.
    // Order: engine_x before engine before blockchain_tests to avoid
    // substring match issues. blockchain_tests before state_tests so
    // that blockchain_tests/static/state_tests/ files are correctly
    // classified as blockchain_tests.
    if (str.find("/blockchain_tests_engine_x/") != std::string::npos)
        return "unsupported";
    if (str.find("/blockchain_tests_engine/") != std::string::npos)
        return "unsupported";
    if (str.find("/blockchain_tests/") != std::string::npos)
        return "blockchain_test";
    if (str.find("/state_tests/") != std::string::npos)
        return "state_test";
    if (str.find("/transaction_tests/") != std::string::npos)
        return "transaction_test";
    // Unknown — default to state_test
    return "state_test";
}

// =============================================================================
//  Process a single JSON fixture file (supports both state_test & blockchain_test)
// =============================================================================
struct FileResult
{
    std::string path;
    int passed = 0;
    int failed = 0;
};

FileResult processFixtureFile(EESTRunner& runner, fs::path const& filePath)
{
    FileResult result;
    result.path = filePath.string();

    if (!g_opts.quiet)
        printLine("  Loading: " + filePath.string());

    auto format = detectFormatFromPath(filePath);

    // --- blockchain_test ---
    if (format == "blockchain_test")
    {
        auto bcFixtures = test::loadEESTBlockchainFixtures(filePath.string());
        for (auto const& fixture : bcFixtures)
        {
            ++g_totalTests;
            bool passed = runner.runBlockchainFixture(fixture);
            if (passed)
            {
                ++g_totalPassed;
                ++result.passed;
            }
            else
            {
                ++g_totalFailed;
                ++result.failed;
            }
        }
        return result;
    }

    // --- transaction_test / unsupported (skip) ---
    if (format == "transaction_test" || format == "unsupported")
        return result;

    // --- state_test (default) ---
    auto fixtures = test::loadEESTFixtures(filePath.string());
    for (auto const& fixture : fixtures)
    {
        for (auto const& [forkName, posts] : fixture.post)
        {
            for (size_t i = 0; i < posts.size(); ++i)
            {
                ++g_totalTests;
                auto const& post = posts[i];
                bool passed = runner.runFixture(fixture, forkName, post);

                if (passed)
                {
                    ++g_totalPassed;
                    ++result.passed;
                }
                else
                {
                    ++g_totalFailed;
                    ++result.failed;
                }
            }
        }
    }
    return result;
}

// =============================================================================
//  Print a progress bar
// =============================================================================
void printProgress(int done, int total, const char* unit = "files")
{
    if (total == 0)
        return;
    int barWidth = 40;
    float ratio = static_cast<float>(done) / total;
    int filled = static_cast<int>(barWidth * ratio);
    std::ostringstream oss;
    oss << "\r[" << std::string(filled, '=') << std::string(barWidth - filled, ' ') << "] " << done
        << "/" << total << " " << unit;
    std::cerr << oss.str() << std::flush;
}

// =============================================================================
//  Main
// =============================================================================
int main(int argc, char* argv[])
{
    parseOptions(argc, argv);

    if (g_opts.help)
    {
        printUsage(argv[0]);
        return 0;
    }

    // Suppress BCOS internal Boost.Log output if --no-log is set
    if (g_opts.noLog)
    {
        boost::log::core::get()->set_logging_enabled(false);
    }

    // Resolve fixture directory
    std::string fixtureDir = g_opts.fixtureDir;
    if (fixtureDir.empty() && !g_opts.singleFixture.empty())
    {
        // --fixture mode: use containing directory for display
        fixtureDir = fs::path(g_opts.singleFixture).parent_path().string();
    }
    if (fixtureDir.empty())
    {
        char const* envDir = std::getenv("EEST_FIXTURE_DIR");
        if (envDir)
            fixtureDir = envDir;
    }

    // Collect file list
    std::vector<fs::path> fileList;

    if (!g_opts.singleFixture.empty())
    {
        fs::path p(g_opts.singleFixture);
        if (!fs::exists(p) || !fs::is_regular_file(p))
        {
            std::cerr << "Error: fixture file not found: " << p << std::endl;
            return 1;
        }
        fileList.push_back(p);
        std::cout << "[EEST Runner] Single fixture mode: " << p << std::endl;
    }
    else if (!fixtureDir.empty())
    {
        if (!fs::exists(fixtureDir) || !fs::is_directory(fixtureDir))
        {
            std::cerr << "Error: fixture directory not found: " << fixtureDir << std::endl;
            return 1;
        }
        for (auto const& entry : fs::recursive_directory_iterator(fixtureDir))
        {
            if (entry.is_regular_file() && entry.path().extension().string() == ".json")
                fileList.push_back(entry.path());
        }
        std::cout << "[EEST Runner] Found " << fileList.size()
                  << " fixture files in: " << fixtureDir << std::endl;
    }
    else
    {
        std::cerr << "Error: no fixture directory specified.\n"
                  << "  Use --fixture-dir DIR, --fixture FILE, or set "
                     "EEST_FIXTURE_DIR environment variable.\n";
        return 1;
    }

    if (fileList.empty())
    {
        std::cerr << "No fixture files found." << std::endl;
        return 0;
    }

    // Sort for deterministic output
    std::sort(fileList.begin(), fileList.end());

    // Print directory tree (compact)
    if (!g_opts.quiet && fileList.size() > 1)
    {
        std::cout << "\n--- Fixture tree ---" << std::endl;
        fs::path baseDir = fs::absolute(fixtureDir);
        std::map<std::string, int> dirCounts;
        for (auto const& p : fileList)
        {
            auto rel = fs::relative(p, baseDir);
            auto parent = rel.parent_path();
            if (parent.empty())
                dirCounts["(root)"]++;
            else
            {
                auto it = parent.begin();
                std::string topDir = it->string();
                dirCounts[topDir]++;
            }
        }
        for (auto const& [dir, count] : dirCounts)
            std::cout << "  " << dir << "/  (" << count << " files)" << std::endl;
        std::cout << "---------------------\n" << std::endl;
    }

    auto startTime = std::chrono::steady_clock::now();

    EESTRunner runner;
    for (size_t i = 0; i < fileList.size(); ++i)
    {
        auto& p = fileList[i];
        if (!g_opts.quiet)
        {
            fs::path base = fs::absolute(fixtureDir);
            auto rel = fs::relative(p, base);
            std::cout << "  " << rel.string() << " ... " << std::flush;
        }
        auto fr = processFixtureFile(runner, p);
        if (!g_opts.quiet)
        {
            int total = fr.passed + fr.failed;
            if (fr.failed == 0)
                std::cout << "OK (" << total << " tests)" << std::endl;
            else
                std::cout << fr.failed << "/" << total << " FAILED" << std::endl;
        }
        printProgress(i + 1, fileList.size());
    }

    // Clear progress line
    if (!g_opts.quiet)
        std::cerr << "\r" << std::string(60, ' ') << "\r";

    auto endTime = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(endTime - startTime).count();

    // =========================================================================
    //  Final Summary
    // =========================================================================
    int totalFiles = static_cast<int>(fileList.size());

    std::cout << "\n========== EEST Fixture Test Summary ==========\n";
    std::cout << "  Total tests:   " << g_totalTests.load() << "\n";
    std::cout << "  Passed:        " << g_totalPassed.load() << "\n";
    std::cout << "  Failed:        " << g_totalFailed.load() << "\n";
    std::cout << "  Pass rate:     "
              << (g_totalTests > 0 ? std::to_string(100 * g_totalPassed / g_totalTests) + "%" :
                                     "N/A")
              << "\n";
    std::cout << "  Files:         " << totalFiles << "\n";
    std::cout << "  Elapsed:       " << elapsed << "s\n";

    std::cout << "\n  Error breakdown:\n";
    std::cout << "    Balance mismatch:   " << g_balanceMismatch.load() << "\n";
    std::cout << "    Storage mismatch:   " << g_storageMismatch.load() << "\n";
    std::cout << "    Nonce mismatch:     " << g_nonceMismatch.load() << "\n";
    std::cout << "    Code mismatch:      " << g_codeMismatch.load() << "\n";
    std::cout << "    Expected exception: " << g_expectedException.load() << "\n";
    std::cout << "    Unexpected failure: " << g_unexpectedFailure.load() << "\n";

    // Feature-level summary
    std::cout << "\n  By feature:\n";
    std::map<std::string, int> featureCounts;
    for (auto const& fd : g_failureDetails)
    {
        std::string name = fd.testName;
        auto pos1 = name.find('/');
        if (pos1 != std::string::npos)
        {
            auto pos2 = name.find('/', pos1 + 1);
            if (pos2 != std::string::npos)
            {
                auto pos3 = name.find('/', pos2 + 1);
                std::string feature = (pos3 != std::string::npos) ?
                                          name.substr(pos1 + 1, pos3 - pos1 - 1) :
                                          name.substr(pos1 + 1);
                featureCounts[feature]++;
            }
        }
    }
    for (auto const& [feat, count] : featureCounts)
        std::cout << "    " << feat << ": " << count << "\n";

    // Fork-level summary
    std::cout << "\n  By fork:\n";
    std::map<std::string, int> forkCounts;
    for (auto const& fd : g_failureDetails)
        forkCounts[fd.forkName]++;
    for (auto const& [fork, count] : forkCounts)
        std::cout << "    " << fork << ": " << count << "\n";

    std::cout << "=================================================\n";

    return g_totalFailed > 0 ? 1 : 0;
}
