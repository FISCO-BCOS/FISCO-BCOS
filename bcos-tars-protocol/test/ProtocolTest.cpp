#include "bcos-tars-protocol/protocol/BlockFactoryImpl.h"
#include "bcos-tars-protocol/protocol/BlockHeaderFactoryImpl.h"
#include "bcos-tars-protocol/protocol/TransactionFactoryImpl.h"
#include "bcos-tars-protocol/protocol/TransactionMetaDataImpl.h"
#include "bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h"
#include "bcos-tars-protocol/protocol/TransactionSubmitResultImpl.h"
#include "bcos-tars-protocol/tars/Block.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/hash/SM3.h>
#include <bcos-crypto/interfaces/crypto/CommonType.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-crypto/signature/sm2/SM2Crypto.h>
#include <bcos-framework/interfaces/protocol/ProtocolTypeDef.h>
#include <bcos-framework/interfaces/protocol/Transaction.h>
#include <bcos-protocol/LogEntry.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <tbb/parallel_for.h>
#include <boost/test/tools/old/interface.hpp>
#include <boost/test/unit_test.hpp>
#include <gsl/span>
#include <memory>

namespace bcostars
{
namespace test
{
struct Fixture
{
    Fixture()
    {
        cryptoSuite =
            std::make_shared<bcos::crypto::CryptoSuite>(std::make_shared<bcos::crypto::SM3>(),
                std::make_shared<bcos::crypto::SM2Crypto>(), nullptr);

        blockHeaderFactory =
            std::make_shared<bcostars::protocol::BlockHeaderFactoryImpl>(cryptoSuite);
        transactionFactory =
            std::make_shared<bcostars::protocol::TransactionFactoryImpl>(cryptoSuite);
        transactionReceiptFactory =
            std::make_shared<bcostars::protocol::TransactionReceiptFactoryImpl>(cryptoSuite);
        blockFactory = std::make_shared<bcostars::protocol::BlockFactoryImpl>(
            cryptoSuite, blockHeaderFactory, transactionFactory, transactionReceiptFactory);
    }

    bcos::crypto::CryptoSuite::Ptr cryptoSuite;
    std::shared_ptr<bcostars::protocol::BlockHeaderFactoryImpl> blockHeaderFactory;
    std::shared_ptr<bcostars::protocol::TransactionFactoryImpl> transactionFactory;
    std::shared_ptr<bcostars::protocol::TransactionReceiptFactoryImpl> transactionReceiptFactory;
    std::shared_ptr<bcostars::protocol::BlockFactoryImpl> blockFactory;
};

BOOST_FIXTURE_TEST_SUITE(TestProtocol, Fixture)

inline std::vector<bcos::bytes> fakeSealerList(
    std::vector<bcos::crypto::KeyPairInterface::Ptr>& _keyPairVec,
    bcos::crypto::SignatureCrypto::Ptr _signImpl, size_t size)
{
    std::vector<bcos::bytes> sealerList;
    for (size_t i = 0; i < size; i++)
    {
        auto keyPair = _signImpl->generateKeyPair();
        _keyPairVec.emplace_back(keyPair);
        sealerList.emplace_back(*(keyPair->publicKey()->encode()));
    }
    return sealerList;
}

BOOST_AUTO_TEST_CASE(transaction)
{
    std::string to("Target");
    bcos::bytes input(bcos::asBytes("Arguments"));
    bcos::u256 nonce(800);

    bcostars::protocol::TransactionFactoryImpl factory(cryptoSuite);
    auto tx = factory.createTransaction(0, to, input, nonce, 100, "testChain", "testGroup", 1000,
        cryptoSuite->signatureImpl()->generateKeyPair());

    tx->verify();
    BOOST_CHECK(!tx->sender().empty());
    auto buffer = tx->encode(false);

    auto decodedTx = factory.createTransaction(buffer, true);

    BOOST_CHECK_EQUAL(tx->hash(), decodedTx->hash());
    BOOST_CHECK_EQUAL(tx->version(), 0);
    BOOST_CHECK_EQUAL(tx->to(), to);
    BOOST_CHECK_EQUAL(bcos::asString(tx->input()), bcos::asString(input));

    BOOST_CHECK_EQUAL(tx->nonce(), nonce);
    BOOST_CHECK_EQUAL(tx->blockLimit(), 100);
    BOOST_CHECK_EQUAL(tx->chainId(), "testChain");
    BOOST_CHECK_EQUAL(tx->groupId(), "testGroup");
    BOOST_CHECK_EQUAL(tx->importTime(), 1000);
    BOOST_CHECK_EQUAL(decodedTx->sender(), tx->sender());

    auto block = blockFactory->createBlock();
    block->appendTransaction(std::move(decodedTx));

    auto blockTx = block->transaction(0);
    BOOST_CHECK_EQUAL(blockTx->sender(), tx->sender());
}

BOOST_AUTO_TEST_CASE(transactionMetaData)
{
    bcos::h256 hash("5feceb66ffc86f38d952786c6d696c79c2dbc239dd4e91b46729d73a27fb57e9",
        bcos::crypto::HashType::FromHex);

    bcostars::protocol::TransactionMetaDataImpl metaData(
        [inner = bcostars::TransactionMetaData()]() mutable { return &inner; });
    metaData.setTo(hash.hex());
    metaData.setTo("Hello world!");

    tars::TarsOutputStream<bcostars::protocol::BufferWriterByteVector> output;
    metaData.inner().writeTo(output);
    bcos::bytes buffer;
    output.swap(buffer);

    bcostars::protocol::TransactionMetaDataImpl metaData2(
        [inner = bcostars::TransactionMetaData()]() mutable { return &inner; });
    tars::TarsInputStream<tars::BufferReader> input;
    input.setBuffer((const char*)buffer.data(), buffer.size());
    metaData2.mutableInner().readFrom(input);

    BOOST_CHECK_EQUAL(metaData2.hash().hex(), metaData.hash().hex());

    bcostars::protocol::TransactionMetaDataImpl metaData3(hash, "Hello world!");
    BOOST_CHECK_EQUAL(metaData3.hash().hex(), hash.hex());
    BOOST_CHECK_EQUAL(metaData3.to(), "Hello world!");
}

BOOST_AUTO_TEST_CASE(transactionReceipt)
{
    bcos::crypto::HashType stateRoot(bcos::asBytes("root1"));
    bcos::u256 gasUsed(8858);
    std::string contractAddress("contract Address!");

    auto logEntries = std::make_shared<std::vector<bcos::protocol::LogEntry>>();
    for (auto i : {1, 2, 3})
    {
        bcos::h256s topics;
        for (auto j : {100, 200, 300})
        {
            topics.push_back(
                bcos::h256(bcos::asBytes("topic: " + boost::lexical_cast<std::string>(j))));
        }
        bcos::protocol::LogEntry entry(
            bcos::asBytes("Address: " + boost::lexical_cast<std::string>(i)), topics,
            bcos::asBytes("Data: " + boost::lexical_cast<std::string>(i)));
        logEntries->emplace_back(entry);
    }
    bcos::bytes output(bcos::asBytes("Output!"));

    bcostars::protocol::TransactionReceiptFactoryImpl factory(cryptoSuite);
    auto receipt = factory.createReceipt(gasUsed, contractAddress,
        std::make_shared<std::vector<bcos::protocol::LogEntry>>(*logEntries), 50, output, 888);

    bcos::bytes buffer;
    receipt->encode(buffer);

    auto decodedReceipt = factory.createReceipt(buffer);

    BOOST_CHECK_EQUAL(receipt->hash().hex(), decodedReceipt->hash().hex());
    BOOST_CHECK_EQUAL(receipt->version(), 0);
    BOOST_CHECK_EQUAL(receipt->gasUsed(), gasUsed);
    BOOST_CHECK_EQUAL(receipt->contractAddress(), contractAddress);
    BOOST_CHECK_EQUAL(receipt->logEntries().size(), logEntries->size());
    for (auto i = 0; i < receipt->logEntries().size(); ++i)
    {
        BOOST_CHECK_EQUAL(receipt->logEntries()[i].address(), (*logEntries)[i].address());
        BOOST_CHECK_EQUAL(
            receipt->logEntries()[i].topics().size(), (*logEntries)[i].topics().size());
        for (auto j = 0; j < receipt->logEntries()[i].topics().size(); ++j)
        {
            BOOST_CHECK_EQUAL(
                receipt->logEntries()[i].topics()[j].hex(), (*logEntries)[i].topics()[j].hex());
        }
        BOOST_CHECK_EQUAL(
            receipt->logEntries()[i].data().toString(), (*logEntries)[i].data().toString());
    }

    BOOST_CHECK_EQUAL(receipt->status(), 50);
    BOOST_CHECK_EQUAL(bcos::asString(receipt->output()), bcos::asString(output));
    BOOST_CHECK_EQUAL(receipt->blockNumber(), 888);
}

BOOST_AUTO_TEST_CASE(block)
{
    auto block = blockFactory->createBlock();
    block->setVersion(883);
    block->setBlockType(bcos::protocol::WithTransactionsHash);

    std::string to("Target");
    bcos::bytes input(bcos::asBytes("Arguments"));
    bcos::u256 nonce(100);

    bcos::crypto::HashType stateRoot(bcos::asBytes("root1"));
    std::string contractAddress("contract Address!");

    // set the blockHeader
    std::vector<bcos::crypto::KeyPairInterface::Ptr> keyPairVec;
    auto sealerList = fakeSealerList(keyPairVec, cryptoSuite->signatureImpl(), 4);

    auto header = block->blockHeader();
    header->setNumber(100);
    header->setGasUsed(1000);
    header->setStateRoot(bcos::crypto::HashType("62384386743874"));
    header->setTimestamp(500);

    header->setSealerList(gsl::span<const bcos::bytes>(sealerList));
    BOOST_CHECK(header->sealerList().size() == 4);

    auto signatureList = std::make_shared<std::vector<bcos::protocol::Signature>>();
    for (int64_t i = 0; i < 2; i++)
    {
        bcos::protocol::Signature signature;
        signature.index = i;
        std::string signatureStr = "signature";
        signature.signature = bcos::bytes(signatureStr.begin(), signatureStr.end());
        signatureList->push_back(signature);
    }
    header->setSignatureList(*signatureList);
    BOOST_CHECK(header->signatureList().size() == 2);
    header->hash();
    BOOST_CHECK(header->signatureList().size() == 2);

    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, 50), [block](const tbb::blocked_range<size_t>& range) {
            for (size_t i = range.begin(); i < range.end(); ++i)
            {
                auto constHeader = block->blockHeaderConst();
                BOOST_CHECK(constHeader->signatureList().size() == 2);
                std::cout << "### getHash:" << constHeader->hash().abridged() << std::endl;

                auto header2 = block->blockHeader();
                BOOST_CHECK(header2->signatureList().size() == 2);
            }
        });
    auto logEntries = std::make_shared<std::vector<bcos::protocol::LogEntry>>();
    for (auto i : {1, 2, 3})
    {
        bcos::h256s topics;
        for (auto j : {100, 200, 300})
        {
            topics.push_back(
                bcos::h256(bcos::asBytes("topic: " + boost::lexical_cast<std::string>(j))));
        }
        bcos::protocol::LogEntry entry(
            bcos::asBytes("Address: " + boost::lexical_cast<std::string>(i)), topics,
            bcos::asBytes("Data: " + boost::lexical_cast<std::string>(i)));
        logEntries->emplace_back(entry);
    }
    bcos::bytes output(bcos::asBytes("Output!"));

    for (size_t i = 0; i < 1000; ++i)
    {
        auto transaction = transactionFactory->createTransaction(
            117, to, input, nonce, i, "testChain", "testGroup", 1000);
        block->appendTransaction(transaction);
        auto txMetaData = blockFactory->createTransactionMetaData(
            transaction->hash(), transaction->hash().abridged());
        block->appendTransactionMetaData(txMetaData);

        auto receipt = transactionReceiptFactory->createReceipt(1000, contractAddress,
            std::make_shared<std::vector<bcos::protocol::LogEntry>>(*logEntries), 50, output, i);
        block->appendReceipt(receipt);
    }

    bcos::bytes buffer;
    BOOST_CHECK_NO_THROW(block->encode(buffer));

    auto decodedBlock = blockFactory->createBlock(buffer);

    BOOST_CHECK(decodedBlock->blockHeader()->sealerList().size() == header->sealerList().size());
    // ensure the sealerlist lifetime
    auto decodedSealerList = decodedBlock->blockHeader()->sealerList();
    for (auto i = 0; i < decodedSealerList.size(); i++)
    {
        BOOST_CHECK(decodedSealerList[i] == sealerList[i]);
    }
    auto decodedBlockHeader = decodedBlock->blockHeader();
    BOOST_CHECK(decodedBlockHeader->signatureList().size() == 2);

    // ensure the blockheader lifetime
    for (auto i = 0; i < decodedBlock->blockHeader()->sealerList().size(); i++)
    {
        BOOST_CHECK(decodedBlock->blockHeader()->sealerList()[i] == sealerList[i]);
        std::cout << "##### decodedSealerList size:"
                  << decodedBlock->blockHeader()->sealerList()[i].size() << std::endl;
    }
    BOOST_CHECK_EQUAL(block->blockHeader()->number(), decodedBlock->blockHeader()->number());
    BOOST_CHECK_EQUAL(block->blockHeader()->gasUsed(), decodedBlock->blockHeader()->gasUsed());
    BOOST_CHECK_EQUAL(block->blockHeader()->stateRoot(), decodedBlock->blockHeader()->stateRoot());
    BOOST_CHECK_EQUAL(block->blockHeader()->timestamp(), block->blockHeader()->timestamp());

    BOOST_CHECK_EQUAL(block->version(), decodedBlock->version());
    BOOST_CHECK_EQUAL(block->blockType(), decodedBlock->blockType());

    BOOST_CHECK_EQUAL(block->transactionsSize(), decodedBlock->transactionsSize());
    BOOST_CHECK_EQUAL(block->transactionsHashSize(), decodedBlock->transactionsHashSize());
    BOOST_CHECK_EQUAL(block->transactionsMetaDataSize(), decodedBlock->transactionsMetaDataSize());
    std::cout << "transactionsMetaDataSize:" << block->transactionsMetaDataSize() << std::endl;
    for (size_t i = 0; i < block->transactionsSize(); ++i)
    {
        {
            auto lhs = block->transaction(i);
            auto rhs = decodedBlock->transaction(i);

            // check if transaction hash re-encode
            auto reencodeBuffer = rhs->encode(false);
            auto redecodeBlock = transactionFactory->createTransaction(reencodeBuffer, false);
            BOOST_CHECK_EQUAL(redecodeBlock->hash().hex(), lhs->hash().hex());

            BOOST_CHECK_EQUAL(lhs->hash().hex(), rhs->hash().hex());
            BOOST_CHECK_EQUAL(lhs->version(), rhs->version());
            BOOST_CHECK_EQUAL(lhs->to(), rhs->to());
            BOOST_CHECK_EQUAL(bcos::asString(lhs->input()), bcos::asString(rhs->input()));

            BOOST_CHECK_EQUAL(lhs->nonce(), rhs->nonce());
            BOOST_CHECK_EQUAL(lhs->blockLimit(), rhs->blockLimit());
            BOOST_CHECK_EQUAL(lhs->chainId(), rhs->chainId());
            BOOST_CHECK_EQUAL(lhs->groupId(), rhs->groupId());
            BOOST_CHECK_EQUAL(lhs->importTime(), rhs->importTime());

            // check the txMetaData
            BOOST_CHECK_EQUAL(block->transactionMetaData(i)->hash(),
                decodedBlock->transactionMetaData(i)->hash());
            BOOST_CHECK_EQUAL(
                block->transactionMetaData(i)->to(), decodedBlock->transactionMetaData(i)->to());
            BOOST_CHECK_EQUAL(block->transactionHash(i), block->transactionHash(i));
        }

        {
            // ensure the transaction's lifetime
            BOOST_CHECK_EQUAL(
                block->transaction(i)->hash().hex(), decodedBlock->transaction(i)->hash().hex());
            BOOST_CHECK_EQUAL(
                block->transaction(i)->version(), decodedBlock->transaction(i)->version());
            BOOST_CHECK_EQUAL(block->transaction(i)->to(), decodedBlock->transaction(i)->to());
            BOOST_CHECK_EQUAL(bcos::asString(block->transaction(i)->input()),
                bcos::asString(decodedBlock->transaction(i)->input()));

            BOOST_CHECK_EQUAL(
                block->transaction(i)->nonce(), decodedBlock->transaction(i)->nonce());
            BOOST_CHECK_EQUAL(
                block->transaction(i)->blockLimit(), decodedBlock->transaction(i)->blockLimit());
            BOOST_CHECK_EQUAL(
                block->transaction(i)->chainId(), decodedBlock->transaction(i)->chainId());
            BOOST_CHECK_EQUAL(
                block->transaction(i)->groupId(), decodedBlock->transaction(i)->groupId());
            BOOST_CHECK_EQUAL(
                block->transaction(i)->importTime(), decodedBlock->transaction(i)->importTime());
        }
    }

    BOOST_CHECK_EQUAL(block->receiptsSize(), decodedBlock->receiptsSize());
    for (size_t i = 0; i < block->receiptsSize(); ++i)
    {
        {
            auto lhs = block->receipt(i);
            auto rhs = decodedBlock->receipt(i);

            BOOST_CHECK_EQUAL(lhs->hash().hex(), rhs->hash().hex());
            BOOST_CHECK_EQUAL(lhs->version(), rhs->version());
            BOOST_CHECK_EQUAL(lhs->gasUsed(), rhs->gasUsed());
            BOOST_CHECK_EQUAL(lhs->contractAddress(), rhs->contractAddress());
            BOOST_CHECK_EQUAL(lhs->logEntries().size(), rhs->logEntries().size());
            for (auto i = 0; i < lhs->logEntries().size(); ++i)
            {
                BOOST_CHECK_EQUAL(lhs->logEntries()[i].address(), rhs->logEntries()[i].address());
                BOOST_CHECK_EQUAL(
                    lhs->logEntries()[i].topics().size(), rhs->logEntries()[i].topics().size());
                for (auto j = 0; j < lhs->logEntries()[i].topics().size(); ++j)
                {
                    BOOST_CHECK_EQUAL(lhs->logEntries()[i].topics()[j].hex(),
                        rhs->logEntries()[i].topics()[j].hex());
                }
                BOOST_CHECK_EQUAL(
                    lhs->logEntries()[i].data().toString(), rhs->logEntries()[i].data().toString());
            }

            BOOST_CHECK_EQUAL(lhs->status(), rhs->status());
            BOOST_CHECK_EQUAL(bcos::asString(lhs->output()), bcos::asString(rhs->output()));
            BOOST_CHECK_EQUAL(lhs->blockNumber(), rhs->blockNumber());
        }

        // ensure the receipt's lifetime
        {
            BOOST_CHECK_EQUAL(
                block->receipt(i)->hash().hex(), decodedBlock->receipt(i)->hash().hex());
            BOOST_CHECK_EQUAL(block->receipt(i)->version(), decodedBlock->receipt(i)->version());
            BOOST_CHECK_EQUAL(block->receipt(i)->gasUsed(), decodedBlock->receipt(i)->gasUsed());
            BOOST_CHECK_EQUAL(
                block->receipt(i)->contractAddress(), decodedBlock->receipt(i)->contractAddress());
            BOOST_CHECK_EQUAL(block->receipt(i)->logEntries().size(),
                decodedBlock->receipt(i)->logEntries().size());
            for (auto i = 0; i < block->receipt(i)->logEntries().size(); ++i)
            {
                BOOST_CHECK_EQUAL(block->receipt(i)->logEntries()[i].address(),
                    decodedBlock->receipt(i)->logEntries()[i].address());
                BOOST_CHECK_EQUAL(block->receipt(i)->logEntries()[i].topics().size(),
                    decodedBlock->receipt(i)->logEntries()[i].topics().size());
                for (auto j = 0; j < block->receipt(i)->logEntries()[i].topics().size(); ++j)
                {
                    BOOST_CHECK_EQUAL(block->receipt(i)->logEntries()[i].topics()[j].hex(),
                        decodedBlock->receipt(i)->logEntries()[i].topics()[j].hex());
                }
                BOOST_CHECK_EQUAL(block->receipt(i)->logEntries()[i].data().toString(),
                    decodedBlock->receipt(i)->logEntries()[i].data().toString());
            }

            BOOST_CHECK_EQUAL(block->receipt(i)->status(), decodedBlock->receipt(i)->status());
            BOOST_CHECK_EQUAL(bcos::asString(block->receipt(i)->output()),
                bcos::asString(decodedBlock->receipt(i)->output()));
            BOOST_CHECK_EQUAL(
                block->receipt(i)->blockNumber(), decodedBlock->receipt(i)->blockNumber());
        }
    }
}

BOOST_AUTO_TEST_CASE(blockHeader)
{
    auto header = blockHeaderFactory->createBlockHeader();

    BOOST_CHECK_EQUAL(header->gasUsed(), bcos::u256(0));

    header->setNumber(100);
    header->setTimestamp(200);

    bcos::u256 gasUsed(1000);
    header->setGasUsed(gasUsed);

    bcos::protocol::ParentInfo parentInfo;
    parentInfo.blockHash = bcos::crypto::HashType(10000);
    parentInfo.blockNumber = 2000;

    std::vector<bcos::protocol::ParentInfo> parentInfoList;
    parentInfoList.emplace_back(parentInfo);

    header->setParentInfo(std::move(parentInfoList));

    bcos::bytes buffer;
    header->encode(buffer);

    auto decodedHeader = blockHeaderFactory->createBlockHeader(buffer);

    BOOST_CHECK_EQUAL(header->number(), decodedHeader->number());
    BOOST_CHECK_EQUAL(header->timestamp(), decodedHeader->timestamp());
    BOOST_CHECK_EQUAL(header->gasUsed(), decodedHeader->gasUsed());
    BOOST_CHECK_EQUAL(header->parentInfo().size(), decodedHeader->parentInfo().size());
    for (int i = 0; i < decodedHeader->parentInfo().size(); ++i)
    {
        BOOST_CHECK_EQUAL(bcos::toString(header->parentInfo()[i].blockHash),
            bcos::toString(decodedHeader->parentInfo()[i].blockHash));
        BOOST_CHECK_EQUAL(
            header->parentInfo()[i].blockNumber, decodedHeader->parentInfo()[i].blockNumber);
    }

    BOOST_CHECK_NO_THROW(header->setExtraData(header->extraData().toBytes()));
}

BOOST_AUTO_TEST_CASE(emptyBlockHeader)
{
    auto blockHeaderFactory =
        std::make_shared<bcostars::protocol::BlockHeaderFactoryImpl>(cryptoSuite);
    auto transactionFactory =
        std::make_shared<bcostars::protocol::TransactionFactoryImpl>(cryptoSuite);
    auto transactionReceiptFactory =
        std::make_shared<bcostars::protocol::TransactionReceiptFactoryImpl>(cryptoSuite);
    bcostars::protocol::BlockFactoryImpl blockFactory(
        cryptoSuite, blockHeaderFactory, transactionFactory, transactionReceiptFactory);

    auto block = blockFactory.createBlock();

    BOOST_CHECK_NO_THROW(block->setBlockHeader(nullptr));
}

BOOST_AUTO_TEST_CASE(submitResult)
{
    protocol::TransactionSubmitResultImpl submitResult(nullptr);
    submitResult.setNonce(bcos::protocol::NonceType("1234567"));

    BOOST_CHECK_EQUAL(submitResult.nonce().str(), "1234567");
}

BOOST_AUTO_TEST_CASE(tarsMovable)
{
    bcostars::Transaction tx1;
    tx1.data.chainID = "chainID";
    std::string input("input data for test");
    tx1.data.input.assign(input.begin(), input.end());

    auto addressTx1 = tx1.data.input.data();

    bcostars::Transaction tx2 = std::move(tx1);

    BOOST_CHECK_EQUAL((intptr_t)addressTx1, (intptr_t)tx2.data.input.data());

    BOOST_CHECK_EQUAL((intptr_t)tx1.data.input.data(), (intptr_t) nullptr);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace test
}  // namespace bcostars