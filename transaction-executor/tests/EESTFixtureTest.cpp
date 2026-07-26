/**
 * @file EESTFixtureTest.cpp
 * @brief Ethereum Execution Spec Tests (EEST) state test fixture runner.
 *
 * Loads EEST v5.4.0 JSON state test fixtures and executes them through
 * bcos-transaction-executor.  Verifies that the post-state matches the
 * expected output for each test case.
 *
 * Usage:
 *   ./test-eest-fixtures --fixture-dir=/path/to/fixtures
 *
 * The fixture directory should contain .json files in EEST state_test format.
 */

#include "../bcos-transaction-executor/TransactionExecutorImpl.h"
#include "EESTFixtureLoader.h"
#include "TestMemoryStorage.h"
#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-executor/src/Common.h"
#include "bcos-framework/ledger/EVMAccount.h"
#include "bcos-framework/ledger/Features.h"
#include "bcos-framework/protocol/Protocol.h"
#include "bcos-tars-protocol/protocol/BlockHeaderImpl.h"
#include "bcos-tars-protocol/protocol/TransactionFactoryImpl.h"
#include "bcos-tars-protocol/protocol/TransactionImpl.h"
#include "bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h"
#include <evmc/evmc.h>
#include <boost/test/unit_test.hpp>
#include <filesystem>
#include <iostream>

using namespace bcos;
using namespace bcos::storage2;
using namespace bcos::executor_v1;
namespace fs = std::filesystem;

// ==================== Test Fixture ====================

class EESTFixtureRunner
{
public:
    MutableStorage storage;
    ledger::LedgerConfig ledgerConfig;
    std::shared_ptr<bcos::crypto::CryptoSuite> cryptoSuite =
        std::make_shared<bcos::crypto::CryptoSuite>(
            std::make_shared<bcos::crypto::Keccak256>(), nullptr, nullptr);
    bcostars::protocol::TransactionFactoryImpl transactionFactory{cryptoSuite};
    bcostars::protocol::TransactionReceiptFactoryImpl receiptFactory{cryptoSuite};
    PrecompiledManager precompiledManager{cryptoSuite->hashImpl()};
    TransactionExecutorImpl executor{receiptFactory, cryptoSuite->hashImpl(), precompiledManager};
    evmc_revision m_currentRevision = EVMC_CANCUN;

    EESTFixtureRunner()
    {
        bcos::executor::GlobalHashImpl::g_hashImpl = std::make_shared<bcos::crypto::Keccak256>();
    }

    /// Configure BCOS LedgerConfig with Ethereum-mode features for the given fork.
    void configureFork(std::string const& forkName)
    {
        auto const rev = test::forkNameToRevision(forkName);
        m_currentRevision = rev;
        ledger::Features features;

        // Enable Ethereum executor mode
        features.set(ledger::Features::Flag::feature_ethereum_executor);

        // Enable all 3.18.0 bugfix flags to match production feature set
        features.setGenesisFeatures(bcos::protocol::BlockVersion::MAX_VERSION);

        // Enable fork-specific features (toRevision maps these → evmc_revision)
        if (rev >= EVMC_OSAKA)
            features.set(ledger::Features::Flag::feature_evm_osaka);
        if (rev >= EVMC_PRAGUE)
            features.set(ledger::Features::Flag::feature_evm_prague);
        if (rev >= EVMC_CANCUN)
            features.set(ledger::Features::Flag::feature_evm_cancun);

        ledgerConfig.setFeatures(features);
        ledgerConfig.setEVMCRevision(rev);
    }

    /// Configure environment-specific settings (base fee, etc.)
    void configureEnvironment(test::EESTEnvironment const& env)
    {
        // Set block base fee from EEST environment (EIP-1559, London+)
        // LedgerConfig::gasPrice() is used as the block baseFee in Ethereum mode
        if (!env.baseFee.empty())
        {
            auto baseFeeVal = test::hexToU256(env.baseFee);
            auto hexStr = "0x" + baseFeeVal.str(0, std::ios_base::hex);
            ledgerConfig.setGasPrice({hexStr, 0});
        }
        else
        {
            ledgerConfig.setGasPrice({"0x0", 0});
        }
    }

    /// Set up pre-state accounts in BCOS storage.
    void setupPreState(std::map<std::string, test::EESTAccount> const& pre)
    {
        for (auto const& [addrHex, acc] : pre)
        {
            auto addrBytes = test::hexToBytes(addrHex);
            if (addrBytes.size() != sizeof(evmc_address))
                continue;

            evmc_address addr;
            std::copy(addrBytes.begin(), addrBytes.end(), addr.bytes);

            ledger::account::EVMAccount<MutableStorage> evmAccount(
                storage, addr, false /* binary address */);

            // Ensure account exists
            task::syncWait([&]() -> task::Task<void> {
                if (!co_await evmAccount.exists())
                    co_await evmAccount.create();
            }());

            // Set nonce (BCOS stores nonce as decimal strings; EEST hex 0→"0", 1→"1" etc.)
            if (!acc.nonce.empty() && acc.nonce != "0x" && acc.nonce != "0x0")
            {
                auto nonceStr = test::hexToU256(acc.nonce).str(0, std::ios_base::dec);
                task::syncWait(
                    [&]() -> task::Task<void> { co_await evmAccount.setNonce(nonceStr); }());
            }

            // Set balance
            if (!acc.balance.empty() && acc.balance != "0x0")
            {
                auto bal = test::hexToU256(acc.balance);
                if (bal > 0)
                {
                    task::syncWait(
                        [&]() -> task::Task<void> { co_await evmAccount.setBalance(bal); }());
                }
            }

            // Set code
            if (!acc.code.empty() && acc.code != "0x")
            {
                auto codeBytes = test::hexToBytes(acc.code);
                if (!codeBytes.empty())
                {
                    auto codeHash = cryptoSuite->hashImpl()->hash(
                        bytesConstRef(codeBytes.data(), codeBytes.size()));
                    task::syncWait([&]() -> task::Task<void> {
                        co_await evmAccount.setCode(std::move(codeBytes), std::string{}, codeHash);
                    }());
                }
            }

            // Set storage
            for (auto const& [key, val] : acc.storage)
            {
                auto keyBytes = test::hexToBytes(key);
                auto valBytes = test::hexToBytes(val);
                if (keyBytes.size() <= 32 && valBytes.size() <= 32)
                {
                    evmc_bytes32 storageKey{}, storageValue{};
                    // EEST hex values are big-endian: right-align in 32-byte slot
                    std::copy(
                        keyBytes.begin(), keyBytes.end(), storageKey.bytes + 32 - keyBytes.size());
                    std::copy(valBytes.begin(), valBytes.end(),
                        storageValue.bytes + 32 - valBytes.size());

                    task::syncWait([&]() -> task::Task<void> {
                        co_await evmAccount.setStorage(storageKey, storageValue);
                    }());
                }
            }
        }
    }

    /// Build a BCOS BlockHeader from the EEST environment.
    bcostars::protocol::BlockHeaderImpl buildBlockHeader(test::EESTEnvironment const& env)
    {
        bcostars::protocol::BlockHeaderImpl header;
        // Map EVM revision to BCOS block version for toRevision() compatibility:
        // - Pre-Cancun with no evm_xxx flag: V3_1→EVMC_LONDON, V3_2→EVMC_PARIS
        // - Cancun+: feature flag overrides, but use V3_1 for uniformity
        uint32_t blockVer;
        if (m_currentRevision >= EVMC_CANCUN)
            blockVer = static_cast<uint32_t>(bcos::protocol::BlockVersion::V3_1_VERSION);
        else if (m_currentRevision >= EVMC_PARIS)
            blockVer = static_cast<uint32_t>(bcos::protocol::BlockVersion::V3_2_VERSION);
        else
            blockVer = static_cast<uint32_t>(bcos::protocol::BlockVersion::V3_1_VERSION);
        header.setVersion(blockVer);
        header.setNumber(test::hexToInt64(env.number));

        // Timestamp: EEST gives seconds since epoch in hex; BCOS uses milliseconds
        auto tsSec = test::hexToInt64(env.timestamp);
        header.setTimestamp(tsSec * 1000);

        // Sealer (BCOS-specific, 0 for tests)
        header.setSealer(0);

        // Coinbase: from EEST environment
        if (!env.coinbase.empty())
        {
            auto cbBytes = test::hexToBytes(env.coinbase);
            header.setCoinbase(std::move(cbBytes));
        }

        header.calculateHash(*cryptoSuite->hashImpl());
        return header;
    }

    /// Build a BCOS Transaction from the EEST transaction and indexes.
    std::shared_ptr<bcostars::protocol::TransactionImpl> buildTransaction(
        test::EESTTransaction const& tx, int dataIndex, int gasIndex, int valueIndex)
    {
        // Pick values using indexes
        std::string txData;
        if (dataIndex >= 0 && static_cast<size_t>(dataIndex) < tx.data.size())
            txData = tx.data[dataIndex];

        std::string gasLimit = "0x0";
        if (gasIndex >= 0 && static_cast<size_t>(gasIndex) < tx.gasLimit.size())
            gasLimit = tx.gasLimit[gasIndex];

        std::string value = "0x0";
        if (valueIndex >= 0 && static_cast<size_t>(valueIndex) < tx.value.size())
            value = tx.value[valueIndex];

        // Build using TransactionFactory
        // createTransaction(version, to, input, abi, nonce, chainId, groupId, blockLimit)
        // But we need to set web3Tx fields. Let's use the low-level approach.
        auto tarsTx = std::make_shared<bcostars::Transaction>();
        auto& data = tarsTx->data;

        data.version = 0;
        data.blockLimit = 0;
        // BCOS's TransactionImpl uses hex2u() to parse these fields, so store
        // EEST hex values as-is (with 0x stripped) — do NOT convert to decimal.
        data.nonce = test::strip0x(tx.nonce);
        data.gasLimit = test::hexToInt64(gasLimit);
        data.value = test::strip0x(value);

        // to address
        if (!tx.to.empty() && tx.to != "0x")
            data.to = test::strip0x(tx.to);

        // input data
        auto inputBytes = test::hexToBytes(txData);
        data.input.assign(inputBytes.begin(), inputBytes.end());

        // Gas price fields: BCOS TransactionImpl uses hex2u(), store as hex without 0x
        if (!tx.gasPrice.empty())
            data.gasPrice = test::strip0x(tx.gasPrice);
        if (!tx.maxFeePerGas.empty())
            data.maxFeePerGas = test::strip0x(tx.maxFeePerGas);
        if (!tx.maxPriorityFeePerGas.empty())
            data.maxPriorityFeePerGas = test::strip0x(tx.maxPriorityFeePerGas);

        // Sender
        if (!tx.sender.empty())
        {
            auto senderBytes = test::hexToBytes(tx.sender);
            tarsTx->sender.assign(senderBytes.begin(), senderBytes.end());
        }
        else if (!tx.secretKey.empty())
        {
            // TODO: derive sender from secret key via ecrecover if needed
        }

        // Chain ID
        data.chainID = "0";  // Will be overridden by LedgerConfig if needed

        tarsTx->type = 1;  // web3 transaction

        // Set a dummy transaction hash (required by TransactionImpl::hash())
        tarsTx->extraTransactionHash.assign(32, 0);

        // Access list (must be built BEFORE determining typed tx kind)
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

        // Determine typed tx kind (must match access list for Web3AccessListResolver)
        // type 2 = EIP-1559 (dynamic fee), type 1 = EIP-2930 (access list),
        // type 4 = EIP-7702 (set_code / authorization list), type 0 = legacy
        bool hasAuthList = tx.authorizationList.has_value() && !tx.authorizationList->empty();
        if (hasAuthList)
            tarsTx->web3TypedTxKind = 4;  // EIP-7702 set_code
        else if (!tx.maxFeePerGas.empty() || !tx.maxPriorityFeePerGas.empty())
            tarsTx->web3TypedTxKind = 2;  // EIP-1559
        else if (hasAccessList)
            tarsTx->web3TypedTxKind = 1;  // EIP-2930 (access list)
        else
            tarsTx->web3TypedTxKind = 0;  // Legacy

        // EIP-7702 authorization list
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

        // EIP-4844 blob versioned hashes
        if (!tx.blobVersionedHashes.empty())
        {
            tarsTx->web3TypedTxKind = 3;  // blob tx
            data.maxFeePerBlobGas = test::strip0x(tx.maxFeePerBlobGas);
            for (auto const& h : tx.blobVersionedHashes)
            {
                auto hashBytes = test::hexToBytes(h);
                data.blobVersionedHashes.emplace_back(hashBytes.begin(), hashBytes.end());
            }
        }

        auto impl = std::make_shared<bcostars::protocol::TransactionImpl>(
            [tarsTx = std::move(tarsTx)]() mutable { return tarsTx.get(); });
        return impl;
    }

    /// Verify post-state matches expected values.
    /// Returns pair of {passed, failures}.
    std::pair<int, int> verifyPostState(std::map<std::string, test::EESTAccount> const& expected)
    {
        int passed = 0;
        int failed = 0;

        for (auto const& [addrHex, expectedAcc] : expected)
        {
            auto addrBytes = test::hexToBytes(addrHex);
            if (addrBytes.size() != sizeof(evmc_address))
                continue;

            evmc_address addr;
            std::copy(addrBytes.begin(), addrBytes.end(), addr.bytes);

            ledger::account::EVMAccount<MutableStorage> evmAccount(
                storage, addr, false /* binary address */);

            bool accPassed = true;

            task::syncWait([&]() -> task::Task<void> {
                // Check nonce
                if (!expectedAcc.nonce.empty() && expectedAcc.nonce != "0x")
                {
                    auto storedNonce = co_await evmAccount.nonce();
                    auto expNonce = test::hexToU256(expectedAcc.nonce);
                    auto actualNonce = bcos::u256(storedNonce.value_or("0"));
                    if (expNonce != actualNonce)
                    {
                        std::cerr << "  NONCE MISMATCH for " << addrHex << ": expected "
                                  << expectedAcc.nonce << ", got " << actualNonce << std::endl;
                        accPassed = false;
                    }
                }

                // Check balance
                if (!expectedAcc.balance.empty() && expectedAcc.balance != "0x")
                {
                    auto storedBal = co_await evmAccount.balance();
                    auto expBal = test::hexToU256(expectedAcc.balance);
                    if (expBal != storedBal)
                    {
                        std::cerr << "  BALANCE MISMATCH for " << addrHex << ": expected "
                                  << expectedAcc.balance << ", got " << storedBal << std::endl;
                        accPassed = false;
                    }
                }

                // Check code
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
                        std::cerr << "  CODE MISMATCH for " << addrHex << ": expected "
                                  << expectedAcc.code << std::endl;
                        accPassed = false;
                    }
                }

                // Check storage
                for (auto const& [key, val] : expectedAcc.storage)
                {
                    auto keyBytes = test::hexToBytes(key);
                    evmc_bytes32 storageKey{};
                    // Key is big-endian: right-align in 32-byte slot (same as value)
                    if (keyBytes.size() <= 32)
                        std::copy(keyBytes.begin(), keyBytes.end(),
                            storageKey.bytes + 32 - keyBytes.size());
                    else
                        std::copy(keyBytes.begin(), keyBytes.end(), storageKey.bytes);

                    auto storedVal = co_await evmAccount.storage(storageKey);
                    auto expValBytes = test::hexToBytes(val);
                    evmc_bytes32 expVal{};
                    // EEST hex values are big-endian: copy to the RIGHT side of the 32-byte slot
                    if (expValBytes.size() <= 32)
                        std::copy(expValBytes.begin(), expValBytes.end(),
                            expVal.bytes + 32 - expValBytes.size());

                    if (!::ranges::equal(storedVal.bytes, expVal.bytes))
                    {
                        std::string actualHex;
                        boost::algorithm::hex_lower(
                            storedVal.bytes, storedVal.bytes + 32, std::back_inserter(actualHex));
                        std::cerr << "  STORAGE MISMATCH for " << addrHex << " key " << key
                                  << ": expected " << val << ", got 0x" << actualHex << std::endl;
                        accPassed = false;
                    }
                }
            }());

            if (accPassed)
                ++passed;
            else
                ++failed;
        }

        return {passed, failed};
    }

    /// Run a single fixture test case.
    /// Returns true if the test passed.
    bool runFixture(test::EESTFixture const& fixture, std::string const& forkName,
        test::EESTForkPost const& post)
    {
        // Configure fork
        configureFork(forkName);

        // Configure environment (base fee etc.)
        configureEnvironment(fixture.env);

        // Set up pre-state
        setupPreState(fixture.pre);

        // Build block header
        auto blockHeader = buildBlockHeader(fixture.env);

        // Build transaction
        auto tx =
            buildTransaction(fixture.transaction, post.dataIndex, post.gasIndex, post.valueIndex);

        // Execute
        protocol::TransactionReceipt::Ptr receipt;
        std::string evmError;
        bool executionThrew = false;

        try
        {
            receipt = task::syncWait(
                executor.executeTransaction(storage, blockHeader, *tx, 0, ledgerConfig, false));
        }
        catch (std::exception const& e)
        {
            evmError = e.what();
            executionThrew = true;
        }

        // Check expectation
        bool expectsSuccess = post.expectException.empty();
        bool gotSuccess =
            !executionThrew && receipt &&
            (receipt->status() == 0 ||
                receipt->status() == static_cast<int32_t>(protocol::TransactionStatus::None));

        if (expectsSuccess && !gotSuccess)
        {
            std::cerr << "FAIL: " << fixture.name << " [" << forkName
                      << "] expected success, got failure"
                      << (executionThrew ? " (exception: " + evmError + ")" : "")
                      << (receipt ? " status=" + std::to_string(receipt->status()) : "")
                      << std::endl;
            return false;
        }

        if (!expectsSuccess && gotSuccess)
        {
            std::cerr << "FAIL: " << fixture.name << " [" << forkName << "] expected exception '"
                      << post.expectException << "', got success" << std::endl;
            return false;
        }

        // If both expected and got failure, that's a pass
        if (!expectsSuccess && !gotSuccess)
            return true;

        // Verify post-state
        auto [passed, failed] = verifyPostState(post.state);
        bool stateOk = (failed == 0);

        if (!stateOk)
        {
            std::cerr << "FAIL: " << fixture.name << " [" << forkName << "] " << passed
                      << " accounts ok, " << failed << " mismatched" << std::endl;
            return false;
        }

        return true;
    }
};

// ==================== Boost Test Suite ====================

BOOST_FIXTURE_TEST_SUITE(EESTStateTests, EESTFixtureRunner)

/// Run all EEST state test fixtures from a directory.
/// Set environment variable EEST_FIXTURE_DIR to point to the fixture directory.
BOOST_AUTO_TEST_CASE(runEESTFixtures)
{
    const char* fixtureDir = std::getenv("EEST_FIXTURE_DIR");
    if (!fixtureDir)
    {
        BOOST_TEST_MESSAGE("EEST_FIXTURE_DIR not set, skipping EEST fixture tests");
        BOOST_TEST_MESSAGE("To run: EEST_FIXTURE_DIR=/path/to/fixtures ./test-eest-fixtures");
        return;
    }

    std::string dirPath(fixtureDir);
    if (!fs::exists(dirPath) || !fs::is_directory(dirPath))
    {
        BOOST_TEST_MESSAGE("EEST_FIXTURE_DIR '" << dirPath << "' does not exist, skipping");
        return;
    }

    int totalTests = 0;
    int totalPassed = 0;
    int totalFailed = 0;

    // Iterate over JSON fixture files recursively
    for (auto const& entry : fs::recursive_directory_iterator(dirPath))
    {
        if (!entry.is_regular_file())
            continue;
        auto ext = entry.path().extension().string();
        if (ext != ".json")
            continue;

        BOOST_TEST_MESSAGE("Loading fixtures from: " << entry.path().string());

        auto fixtures = test::loadEESTFixtures(entry.path().string());

        for (auto const& fixture : fixtures)
        {
            for (auto const& [forkName, posts] : fixture.post)
            {
                // Skip forks we don't support well yet
                auto rev = test::forkNameToRevision(forkName);

                for (size_t i = 0; i < posts.size(); ++i)
                {
                    ++totalTests;

                    // Re-initialize storage for each test
                    storage = MutableStorage{};

                    auto const& post = posts[i];
                    bool passed = runFixture(fixture, forkName, post);

                    if (passed)
                    {
                        ++totalPassed;
                    }
                    else
                    {
                        ++totalFailed;
                    }
                }
            }
        }
    }

    BOOST_TEST_MESSAGE("EEST fixture results: " << totalPassed << "/" << totalTests << " passed, "
                                                << totalFailed << " failed");

    BOOST_CHECK_EQUAL(totalFailed, 0);
}

BOOST_AUTO_TEST_SUITE_END()
