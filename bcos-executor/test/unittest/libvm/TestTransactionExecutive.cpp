#include "../../src/executive/TransactionExecutive.h"
#include "fixture/TransactionFixture.h"

#include <bcos-crypto/ChecksumAddress.h>
#include <bcos-framework/ledger/Account.h>
#include <bcos-framework/ledger/EVMAccount.h>

#include <boost/test/unit_test.hpp>
#include <utility>

namespace bcos::test
{
class TransactionExecutiveFixture : public TransactionFixture
{
public:
    TransactionExecutiveFixture() : TransactionFixture()
    {
        setIsWasm(false);
        web3Features.set(Features::Flag::feature_balance);
        web3Features.set(Features::Flag::feature_balance_precompiled);
        web3Features.set(Features::Flag::feature_evm_address);
        web3Features.set(Features::Flag::feature_evm_cancun);
        web3Features.set(Features::Flag::feature_evm_timestamp);
    }

    auto deployTestContract(uint blockNumber, TransactionType type)
    {
        nextBlock(blockNumber, protocol::BlockVersion::MAX_VERSION, web3Features);
        auto nonce = blockNumber + 100;
        bytes in = fromHex(deployAddressBin);
        auto tx = fakeTransaction(
            cryptoSuite, keyPair, "", in, std::to_string(nonce), 100001, "1", "1", "");
        auto hash = tx->hash();
        txpool->hash2Transaction[hash] = tx;


        auto const sender = keyPair->address(hashImpl);
        auto const address = newLegacyEVMAddressString(sender.ref(), u256(nonce));

        bcos::ledger::account::EVMAccount eoa(*storage, sender.hex(), false);
        task::syncWait(bcos::ledger::account::create(eoa));
        task::syncWait(bcos::ledger::account::setBalance(eoa, u256(1000000000000000ULL)));

        auto params = std::make_unique<NativeExecutionMessage>();
        params->setNonce(toQuantity(nonce));
        params->setTransactionHash(hash);
        params->setContextID(blockNumber);
        params->setSeq(0);
        params->setDepth(0);
        params->setFrom(sender.hex());
        params->setTo(address);
        params->setOrigin(sender.hex());
        params->setStaticCall(false);
        params->setGasAvailable(gas);
        params->setCreate(true);
        params->setData(std::move(in));
        params->setType(NativeExecutionMessage::TXHASH);
        params->setTxType(static_cast<uint>(type));

        std::promise<ExecutionMessage::UniquePtr> executePromise;
        executor->executeTransaction(std::move(params), [&](auto&& error, auto&& result) {
            BOOST_CHECK(!error);
            executePromise.set_value(std::move(result));
        });

        auto result = executePromise.get_future().get();

        BOOST_CHECK_EQUAL(result->status(), 0);

        if (type != TransactionType::BCOSTransaction)
        {
            std::unordered_map<std::string, u256> eoaNonceMap;
            eoaNonceMap[sender.hex()] = nonce;
            executor->updateEoaNonce(std::move(eoaNonceMap));
        }

        commitBlock(blockNumber);

        auto const nonceOfEoa = task::syncWait(bcos::ledger::account::nonce(eoa));

        bcos::ledger::account::EVMAccount contractAddress(*storage, address, false);
        auto const nonceOfContract = task::syncWait(bcos::ledger::account::nonce(contractAddress));
        if (type == TransactionType::BCOSTransaction)
        {
            BOOST_CHECK_EQUAL(nonceOfContract.has_value(), true);
            BOOST_CHECK_EQUAL(nonceOfContract.value(), "1");
        }
        else
        {
            BOOST_CHECK_EQUAL(nonceOfEoa.value(), std::to_string(nonce + 1));
            BOOST_CHECK_EQUAL(nonceOfContract.value(), "1");
        }
        return address;
    }

    auto newHelloWorld(uint blockNumber, TransactionType type, std::string contractAddress,
        uint expectContractNonce)
    {
        nextBlock(blockNumber, protocol::BlockVersion::MAX_VERSION, web3Features);
        auto nonce = blockNumber + 100;
        bytes in = codec->encodeWithSig("newHello()");
        auto tx = fakeTransaction(
            cryptoSuite, keyPair, "", in, std::to_string(nonce), 100001, "1", "1", "");
        auto hash = tx->hash();
        txpool->hash2Transaction[hash] = tx;

        auto const sender = keyPair->address(hashImpl);

        bcos::ledger::account::EVMAccount eoa(*storage, sender.hex(), false);
        task::syncWait(bcos::ledger::account::create(eoa));
        task::syncWait(bcos::ledger::account::setBalance(eoa, u256(1000000000000000ULL)));

        auto params = std::make_unique<NativeExecutionMessage>();
        params->setNonce(toQuantity(nonce));
        params->setTransactionHash(hash);
        params->setContextID(blockNumber);
        params->setSeq(0);
        params->setDepth(0);
        params->setFrom(sender.hex());
        params->setTo(contractAddress);
        params->setOrigin(sender.hex());
        params->setStaticCall(false);
        params->setGasAvailable(gas);
        params->setCreate(false);
        params->setData(std::move(in));
        params->setType(NativeExecutionMessage::TXHASH);
        params->setTxType(static_cast<uint>(type));

        std::promise<ExecutionMessage::UniquePtr> executePromise;
        executor->executeTransaction(std::move(params), [&](auto&& error, auto&& result) {
            BOOST_CHECK(!error);
            executePromise.set_value(std::move(result));
        });

        auto result = executePromise.get_future().get();

        BOOST_CHECK_EQUAL(result->status(), 0);

        if (type != TransactionType::BCOSTransaction)
        {
            std::unordered_map<std::string, u256> eoaNonceMap;
            eoaNonceMap[sender.hex()] = nonce;
            executor->updateEoaNonce(std::move(eoaNonceMap));
        }
        commitBlock(blockNumber);

        auto const nonceOfEoa = task::syncWait(bcos::ledger::account::nonce(eoa));

        bcos::ledger::account::EVMAccount contractAccount(*storage, contractAddress, false);
        auto const nonceOfContract = task::syncWait(bcos::ledger::account::nonce(contractAccount));
        if (type == TransactionType::BCOSTransaction)
        {
            BOOST_CHECK_EQUAL(nonceOfContract.has_value(), true);
            BOOST_CHECK_EQUAL(nonceOfContract.value(), std::to_string(expectContractNonce));
        }
        else
        {
            BOOST_CHECK_EQUAL(nonceOfEoa.value(), std::to_string(nonce + 1));
            BOOST_CHECK_EQUAL(nonceOfContract.value(), std::to_string(expectContractNonce));
        }
    }

    auto newRevertHello(uint blockNumber, TransactionType type, std::string contractAddress,
        uint expectContractNonce)
    {
        nextBlock(blockNumber, protocol::BlockVersion::MAX_VERSION, web3Features);
        auto nonce = blockNumber + 100;
        bytes in = codec->encodeWithSig("newRevertHello()");
        auto tx = fakeTransaction(
            cryptoSuite, keyPair, "", in, std::to_string(nonce), 100001, "1", "1", "");
        auto hash = tx->hash();
        txpool->hash2Transaction[hash] = tx;

        auto const sender = keyPair->address(hashImpl);

        bcos::ledger::account::EVMAccount eoa(*storage, sender.hex(), false);
        task::syncWait(bcos::ledger::account::create(eoa));
        task::syncWait(bcos::ledger::account::setBalance(eoa, u256(1000000000000000ULL)));

        auto params = std::make_unique<NativeExecutionMessage>();
        params->setNonce(toQuantity(nonce));
        params->setTransactionHash(hash);
        params->setContextID(blockNumber);
        params->setSeq(0);
        params->setDepth(0);
        params->setFrom(sender.hex());
        params->setTo(contractAddress);
        params->setOrigin(sender.hex());
        params->setStaticCall(false);
        params->setGasAvailable(gas);
        params->setCreate(false);
        params->setData(std::move(in));
        params->setType(NativeExecutionMessage::TXHASH);
        params->setTxType(static_cast<uint>(type));

        std::promise<ExecutionMessage::UniquePtr> executePromise;
        executor->executeTransaction(std::move(params), [&](auto&& error, auto&& result) {
            BOOST_CHECK(!error);
            executePromise.set_value(std::move(result));
        });

        auto result = executePromise.get_future().get();

        BOOST_CHECK_EQUAL(result->status(), 16);

        if (type != TransactionType::BCOSTransaction)
        {
            std::unordered_map<std::string, u256> eoaNonceMap;
            eoaNonceMap[sender.hex()] = nonce;
            executor->updateEoaNonce(std::move(eoaNonceMap));
        }
        commitBlock(blockNumber);

        auto const nonceOfEoa = task::syncWait(bcos::ledger::account::nonce(eoa));

        bcos::ledger::account::EVMAccount contractAccount(*storage, contractAddress, false);
        auto const nonceOfContract = task::syncWait(bcos::ledger::account::nonce(contractAccount));
        if (type == TransactionType::BCOSTransaction)
        {
            BOOST_CHECK_EQUAL(nonceOfContract.has_value(), true);
            BOOST_CHECK_EQUAL(nonceOfContract.value(), std::to_string(expectContractNonce));
        }
        else
        {
            BOOST_CHECK_EQUAL(nonceOfEoa.value(), std::to_string(nonce + 1));
            BOOST_CHECK_EQUAL(nonceOfContract.value(), std::to_string(expectContractNonce));
        }
    }

    auto transfer(uint blockNumber, TransactionType type, std::string toAddress,
        uint64_t initBalance, uint64_t transferValue, uint64_t initNonce)
    {
        nextBlock(blockNumber, protocol::BlockVersion::MAX_VERSION, web3Features);
        auto nonce = initNonce;

        keyPair = cryptoSuite->signatureImpl()->generateKeyPair();
        auto tx = fakeTransaction(cryptoSuite, keyPair, "", {}, std::to_string(nonce), 100001, "1",
            "1", "", 0, transferValue);
        auto hash = tx->hash();
        txpool->hash2Transaction[hash] = tx;

        auto sender = keyPair->address(hashImpl).hex();

        bcos::ledger::account::EVMAccount eoa(*storage, sender, false);
        task::syncWait(bcos::ledger::account::create(eoa));
        task::syncWait(bcos::ledger::account::setBalance(eoa, u256(initBalance)));

        auto params = std::make_unique<NativeExecutionMessage>();
        params->setTransactionHash(hash);
        params->setNonce(toQuantity(nonce));
        params->setContextID(blockNumber);
        params->setSeq(0);
        params->setDepth(0);
        params->setFrom(sender);
        params->setTo(std::move(toAddress));
        params->setOrigin(sender);
        params->setStaticCall(false);
        params->setGasAvailable(gas);
        params->setCreate(false);
        params->setValue(toQuantity(transferValue));
        params->setType(NativeExecutionMessage::TXHASH);
        params->setTxType(static_cast<uint>(type));

        std::promise<ExecutionMessage::UniquePtr> executePromise;
        executor->executeTransaction(std::move(params), [&](auto&& error, auto&& result) {
            BOOST_CHECK(!error);
            executePromise.set_value(std::forward<decltype(result)>(result));
        });

        auto result = executePromise.get_future().get();

        if (type != TransactionType::BCOSTransaction)
        {
            std::unordered_map<std::string, u256> eoaNonceMap;
            eoaNonceMap[sender] = nonce;
            executor->updateEoaNonce(std::move(eoaNonceMap));
        }

        commitBlock(blockNumber);

        auto const nonceOfEoa = task::syncWait(bcos::ledger::account::nonce(eoa));

        if (type == TransactionType::BCOSTransaction)
        {
            if (nonceOfEoa.has_value())
            {
                BOOST_CHECK_EQUAL(nonceOfEoa.value(), std::to_string(nonce));
            }
        }
        else
        {
            BOOST_CHECK_EQUAL(nonceOfEoa.value(), std::to_string(nonce + 1));
        }

        return result;
    }

    auto rawNewHelloWorld(uint blockNumber, TransactionType type, std::string contractAddress,
        RANGES::input_range auto nonces)
    {
        auto const sender = keyPair->address(hashImpl);
        bcos::ledger::account::EVMAccount eoa(*storage, sender.hex(), false);
        task::syncWait(bcos::ledger::account::create(eoa));
        task::syncWait(bcos::ledger::account::setBalance(eoa, u256(1000000000000000ULL)));

        for (auto it = nonces.begin(); it != nonces.end(); ++it)
        {
            auto nonce = *it;
            bytes in = codec->encodeWithSig("newHello()");
            auto tx = fakeTransaction(
                cryptoSuite, keyPair, "", in, std::to_string(nonce), 100001, "1", "1", "");
            auto hash = tx->hash();
            txpool->hash2Transaction[hash] = tx;


            auto params = std::make_unique<NativeExecutionMessage>();
            params->setNonce(toQuantity(nonce));
            params->setTransactionHash(hash);
            params->setContextID(blockNumber);
            params->setSeq(RANGES::distance(RANGES::begin(nonces), it));
            params->setDepth(0);
            params->setFrom(sender.hex());
            params->setTo(contractAddress);
            params->setOrigin(sender.hex());
            params->setStaticCall(false);
            params->setGasAvailable(gas);
            params->setCreate(false);
            params->setData(std::move(in));
            params->setType(NativeExecutionMessage::TXHASH);
            params->setTxType(static_cast<uint>(type));

            std::promise<ExecutionMessage::UniquePtr> executePromise;
            executor->executeTransaction(std::move(params), [&](auto&& error, auto&& result) {
                BOOST_CHECK(!error);
                executePromise.set_value(std::move(result));
            });

            auto result = executePromise.get_future().get();

            BOOST_CHECK_EQUAL(result->status(), 0);
        }

        if (type != TransactionType::BCOSTransaction)
        {
            std::unordered_map<std::string, u256> eoaNonceMap;
            for (auto nonce : nonces)
            {
                auto [it, inserted] = eoaNonceMap.try_emplace(sender.hex(), nonce);
                if (!inserted && it->second < nonce)
                {
                    it->second = nonce;
                }
            }
            executor->updateEoaNonce(std::move(eoaNonceMap));
        }
    }

    Features web3Features{};
    // clang-format off
    static constexpr std::string_view deployAddressBin = "608060405234801561001057600080fd5b506109c3806100206000396000f3fe608060405234801561001057600080fd5b506004361061004c5760003560e01c806319ff1d21146100515780637567e6711461006f5780639ac4d2e614610079578063cb2ade2d14610097575b600080fd5b6100596100a1565b6040516100669190610258565b60405180910390f35b6100776100c5565b005b61008161012f565b60405161008e9190610294565b60405180910390f35b61009f610155565b005b60008054906101000a900473ffffffffffffffffffffffffffffffffffffffff1681565b6040516100d1906101c0565b604051809103906000f0801580156100ed573d6000803e3d6000fd5b506000806101000a81548173ffffffffffffffffffffffffffffffffffffffff021916908373ffffffffffffffffffffffffffffffffffffffff160217905550565b600160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1681565b604051610161906101cd565b604051809103906000f08015801561017d573d6000803e3d6000fd5b50600160006101000a81548173ffffffffffffffffffffffffffffffffffffffff021916908373ffffffffffffffffffffffffffffffffffffffff160217905550565b61061d806102b083390190565b60c1806108cd83390190565b600073ffffffffffffffffffffffffffffffffffffffff82169050919050565b6000819050919050565b600061021e610219610214846101d9565b6101f9565b6101d9565b9050919050565b600061023082610203565b9050919050565b600061024282610225565b9050919050565b61025281610237565b82525050565b600060208201905061026d6000830184610249565b92915050565b600061027e82610225565b9050919050565b61028e81610273565b82525050565b60006020820190506102a96000830184610285565b9291505056fe608060405234801561001057600080fd5b506040518060400160405280600d81526020017f48656c6c6f2c20576f726c6421000000000000000000000000000000000000008152506000908051906020019061005c929190610062565b50610166565b82805461006e90610134565b90600052602060002090601f01602090048101928261009057600085556100d7565b82601f106100a957805160ff19168380011785556100d7565b828001600101855582156100d7579182015b828111156100d65782518255916020019190600101906100bb565b5b5090506100e491906100e8565b5090565b5b808211156101015760008160009055506001016100e9565b5090565b7f4e487b7100000000000000000000000000000000000000000000000000000000600052602260045260246000fd5b6000600282049050600182168061014c57607f821691505b602082108114156101605761015f610105565b5b50919050565b6104a8806101756000396000f3fe608060405234801561001057600080fd5b50600436106100365760003560e01c80634ed3885e1461003b5780636d4ce63c14610057575b600080fd5b6100556004803603810190610050919061031e565b610075565b005b61005f61008f565b60405161006c91906103ef565b60405180910390f35b806000908051906020019061008b929190610121565b5050565b60606000805461009e90610440565b80601f01602080910402602001604051908101604052809291908181526020018280546100ca90610440565b80156101175780601f106100ec57610100808354040283529160200191610117565b820191906000526020600020905b8154815290600101906020018083116100fa57829003601f168201915b5050505050905090565b82805461012d90610440565b90600052602060002090601f01602090048101928261014f5760008555610196565b82601f1061016857805160ff1916838001178555610196565b82800160010185558215610196579182015b8281111561019557825182559160200191906001019061017a565b5b5090506101a391906101a7565b5090565b5b808211156101c05760008160009055506001016101a8565b5090565b6000604051905090565b600080fd5b600080fd5b600080fd5b600080fd5b6000601f19601f8301169050919050565b7f4e487b7100000000000000000000000000000000000000000000000000000000600052604160045260246000fd5b61022b826101e2565b810181811067ffffffffffffffff8211171561024a576102496101f3565b5b80604052505050565b600061025d6101c4565b90506102698282610222565b919050565b600067ffffffffffffffff821115610289576102886101f3565b5b610292826101e2565b9050602081019050919050565b82818337600083830152505050565b60006102c16102bc8461026e565b610253565b9050828152602081018484840111156102dd576102dc6101dd565b5b6102e884828561029f565b509392505050565b600082601f830112610305576103046101d8565b5b81356103158482602086016102ae565b91505092915050565b600060208284031215610334576103336101ce565b5b600082013567ffffffffffffffff811115610352576103516101d3565b5b61035e848285016102f0565b91505092915050565b600081519050919050565b600082825260208201905092915050565b60005b838110156103a1578082015181840152602081019050610386565b838111156103b0576000848401525b50505050565b60006103c182610367565b6103cb8185610372565b93506103db818560208601610383565b6103e4816101e2565b840191505092915050565b6000602082019050818103600083015261040981846103b6565b905092915050565b7f4e487b7100000000000000000000000000000000000000000000000000000000600052602260045260246000fd5b6000600282049050600182168061045857607f821691505b6020821081141561046c5761046b610411565b5b5091905056fea2646970667358221220fc2b2f46c09486df7da2ceda6c91e6ec6a82d3f4580f53886923b172269e72a264736f6c634300080b00336080604052348015600f57600080fd5b506040517f08c379a000000000000000000000000000000000000000000000000000000000815260040160409060a2565b60405180910390fd5b600082825260208201905092915050565b7f52657665727448656c6c6f000000000000000000000000000000000000000000600082015250565b6000608e600b836049565b9150609782605a565b602082019050919050565b6000602082019050818103600083015260b9816083565b905091905056fea2646970667358221220e53006013391463b1160cd453611e0f55dde2ae96ad5beb21bcaa6c25715d14964736f6c634300080b0033";
    // clang-format on
};

BOOST_FIXTURE_TEST_SUITE(testTransactionExecutive, TransactionExecutiveFixture)

BOOST_AUTO_TEST_CASE(testNonceInTransaction)
{
    deployTestContract(1, TransactionType::BCOSTransaction);
    deployTestContract(2, TransactionType::Web3Transaction);
}

BOOST_AUTO_TEST_CASE(testNonceInNewContract)
{
    uint64_t blockNumber = 1;
    deployTestContract(blockNumber++, TransactionType::BCOSTransaction);
    auto const address = deployTestContract(blockNumber++, TransactionType::Web3Transaction);
    newHelloWorld(blockNumber++, TransactionType::BCOSTransaction, address, 2);
    newHelloWorld(blockNumber++, TransactionType::Web3Transaction, address, 3);
    newHelloWorld(blockNumber++, TransactionType::Web3Transaction, address, 4);
    newHelloWorld(blockNumber++, TransactionType::Web3Transaction, address, 5);
    newHelloWorld(blockNumber++, TransactionType::BCOSTransaction, address, 6);
}

BOOST_AUTO_TEST_CASE(testNonceInRevertContract)
{
    uint64_t blockNumber = 1;
    deployTestContract(blockNumber++, TransactionType::BCOSTransaction);
    auto const address = deployTestContract(blockNumber++, TransactionType::Web3Transaction);
    newRevertHello(blockNumber++, TransactionType::BCOSTransaction, address, 1);
    newRevertHello(blockNumber++, TransactionType::Web3Transaction, address, 1);
    newRevertHello(blockNumber++, TransactionType::Web3Transaction, address, 1);
    newRevertHello(blockNumber++, TransactionType::Web3Transaction, address, 1);
    newHelloWorld(blockNumber++, TransactionType::Web3Transaction, address, 2);
    newRevertHello(blockNumber++, TransactionType::BCOSTransaction, address, 2);
    newHelloWorld(blockNumber++, TransactionType::BCOSTransaction, address, 3);
    newRevertHello(blockNumber++, TransactionType::Web3Transaction, address, 3);
}

BOOST_AUTO_TEST_CASE(testNonceInTransfer)
{
    uint64_t blockNumber = 1;
    auto result = transfer(
        blockNumber++, TransactionType::BCOSTransaction, "0x1234567890", 1000000000UL, 10000UL, 0);
    BOOST_CHECK_EQUAL(result->status(), 0);
    // transfer balance not enough, and nonce will still update
    result = transfer(
        blockNumber++, TransactionType::Web3Transaction, "0x1234567890", 1000UL, 10000000UL, 0);
    BOOST_CHECK_EQUAL(result->status(), 7);

    result = transfer(
        blockNumber++, TransactionType::Web3Transaction, "0x1234567890", 10000000UL, 10000000UL, 0);
    BOOST_CHECK_EQUAL(result->status(), 0);
}

BOOST_AUTO_TEST_CASE(testMultiNonce)
{
    uint64_t blockNumber = 1;
    auto const address = deployTestContract(blockNumber++, TransactionType::Web3Transaction);
    auto newBlock = blockNumber++;
    nextBlock(newBlock, protocol::BlockVersion::MAX_VERSION, web3Features);
    // [10000, 10020)
    rawNewHelloWorld(
        newBlock, TransactionType::Web3Transaction, address, RANGES::views::iota(10000, 10020));
    commitBlock(newBlock);

    auto const sender = keyPair->address(hashImpl);
    bcos::ledger::account::EVMAccount eoa(*storage, sender.hex(), false);
    auto const nonceOfEoa = task::syncWait(bcos::ledger::account::nonce(eoa));

    BOOST_CHECK_EQUAL(nonceOfEoa.value(), "10020");
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test