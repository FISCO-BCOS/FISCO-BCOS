/*
 *  Copyright (C) 2021 FISCO BCOS.
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
 * @brief the tool to read and modify data of storage
 * @file archiveTool.cpp
 * @author: xingqiangbai
 * @date 2022-11-08
 */

#include "bcos-framework/ledger/LedgerTypeDef.h"
#include "bcos-framework/storage/StorageInterface.h"
#include "bcos-ledger/src/libledger/Ledger.h"
#include "bcos-ledger/src/libledger/utilities/Common.h"
#include "bcos-rpc/jsonrpc/JsonRpcImpl_2_0.h"
#include "bcos-storage/bcos-storage/TiKVStorage.h"
#include "bcos-tars-protocol/bcos-tars-protocol/protocol/TransactionImpl.h"
#include "bcos-utilities/BoostLogInitializer.h"
#include "boost/filesystem.hpp"
#include "libinitializer/ProtocolInitializer.h"
#include "libinitializer/StorageInitializer.h"
#include "rocksdb/db.h"
#include "rocksdb/options.h"
#include "rocksdb/slice.h"
#include "tikv_client.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/hash/SM3.h>
#include <bcos-crypto/signature/key/KeyFactoryImpl.h>
#include <bcos-security/bcos-security/DataEncryption.h>
#include <bcos-storage/RocksDBStorage.h>
#include <bcos-table/src/KeyPageStorage.h>
#include <json/value.h>
#include <sys/queue.h>
#include <boost/algorithm/hex.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/join.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/program_options.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/throw_exception.hpp>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

using namespace std;
using namespace rocksdb;
using namespace bcos;
using namespace bcos::storage;
using namespace bcos::initializer;
namespace beast = boost::beast;  // from <boost/beast.hpp>
namespace http = beast::http;    // from <boost/beast/http.hpp>
namespace net = boost::asio;     // from <boost/asio.hpp>
using tcp = net::ip::tcp;        // from <boost/asio/ip/tcp.hpp>


namespace fs = boost::filesystem;
namespace po = boost::program_options;

po::options_description main_options(
    "archive tool used to archive/reimport the data of FISCO BCOS v3");

po::variables_map initCommandLine(int argc, const char* argv[])
{
    main_options.add_options()("help,h", "help of storage tool")("archive,a",
        po::value<vector<string>>()->multitoken(),
        "[startBlock] [endBlock] the range is [start,end) which means the end is not archived")(
        "reimport,r", po::value<vector<string>>()->multitoken(),
        "[startBlock] [endBlock] the range is [start,end) which means the end is not reimported")(
        "config,c", boost::program_options::value<std::string>()->default_value("./config.ini"),
        "config file path")("genesis,g",
        boost::program_options::value<std::string>()->default_value("./config.genesis"),
        "genesis config file path")("path,p", boost::program_options::value<std::string>(),
        "the path to store the archived data or read archived data if reimport, if set path then "
        "use rocksDB")("endpoint,e", boost::program_options::value<std::string>(),
        "the ip and port of node archive service in format of IP:Port, ipv6 is not supported")("pd",
        boost::program_options::value<std::string>(),
        "pd address of TiKV, if set use TiKV to archive data of reimport from TiKV, multi address "
        "is split by comma");
    po::variables_map varMap;
    try
    {
        po::store(po::parse_command_line(argc, argv, main_options), varMap);
        po::notify(varMap);
    }
    catch (...)
    {
        std::cout << "parse_command_line failed" << std::endl;
        std::cout << main_options << std::endl;
        exit(0);
    }
    if ((varMap.count("help") != 0U) || (varMap.count("h") != 0U))
    {
        std::cout << main_options << std::endl;
        exit(0);
    }
    return varMap;
}

DB* createSecondaryRocksDB(const std::string& path, const std::string& secondaryPath)
{
    Options options;
    options.create_if_missing = false;
    options.max_open_files = -1;
    DB* db_secondary = nullptr;
    Status status = DB::OpenAsSecondary(options, path, secondaryPath, &db_secondary);
    if (!status.ok())
    {
        std::cout << "open rocksDB failed: " << status.ToString() << std::endl;
        exit(1);
    }
    status = db_secondary->TryCatchUpWithPrimary();
    if (!status.ok())
    {
        std::cout << "TryCatchUpWithPrimary failed: " << status.ToString() << std::endl;
        exit(1);
    }
    return db_secondary;
}

TransactionalStorageInterface::Ptr createBackendStorage(
    std::shared_ptr<bcos::tool::NodeConfig> nodeConfig, const std::string& logPath, bool write,
    const std::string& secondaryPath)
{
    bcos::storage::TransactionalStorageInterface::Ptr storage = nullptr;
    if (boost::iequals(nodeConfig->storageType(), "RocksDB"))
    {
        bcos::security::DataEncryption::Ptr dataEncryption = nullptr;
        if (nodeConfig->storageSecurityEnable())
        {
            dataEncryption = std::make_shared<bcos::security::DataEncryption>(nodeConfig);
            dataEncryption->init();
        }
        if (write)
        {
            storage = StorageInitializer::build(
                nodeConfig->storagePath(), dataEncryption, nodeConfig->keyPageSize());
        }
        else
        {
            auto* rocksdb = createSecondaryRocksDB(nodeConfig->storagePath(), secondaryPath);
            storage = std::make_shared<RocksDBStorage>(
                std::unique_ptr<rocksdb::DB>(rocksdb), dataEncryption);
        }
    }
    else if (boost::iequals(nodeConfig->storageType(), "TiKV"))
    {
#ifdef WITH_TIKV
        storage = StorageInitializer::build(nodeConfig->pdAddrs(), logPath, nodeConfig->pdCaPath(),
            nodeConfig->pdCertPath(), nodeConfig->pdKeyPath());
#endif
    }
    else
    {
        throw std::runtime_error("storage type not support");
    }
    return storage;
}

void deleteArchivedBlocksInNode(const std::string& endpoint, int64_t startBlock, int64_t endBlock)
{
    try
    {
        std::vector<std::string> ipPort;
        boost::split(ipPort, endpoint, boost::is_any_of(":"));
        auto const host = ipPort[0];
        auto const port = ipPort[1];

        // The io_context is required for all I/O
        net::io_context ioc;

        // These objects perform our I/O
        tcp::resolver resolver(ioc);
        beast::tcp_stream stream(ioc);

        // Look up the domain name
        auto const results = resolver.resolve(host, port);

        // Make the connection on the IP address we get from a lookup
        stream.connect(results);

        Json::Value request;
        request["id"] = 1;
        request["jsonrpc"] = "2.0";
        request["method"] = "deleteArchivedData";
        Json::Value params(Json::arrayValue);
        params.append(startBlock);
        params.append(endBlock);
        request["params"] = params;

        http::request<http::string_body> req{http::verb::post, "/", 11};
        req.set(http::field::host, endpoint);
        req.set(http::field::accept, "*/*");
        req.set(http::field::content_type, "application/json");
        req.set(http::field::accept_charset, "utf-8");

        req.body() = request.toStyledString();
        req.prepare_payload();

        // Send the HTTP request to the remote host
        http::write(stream, req);
        std::cout << "---------------request send:\n" << request.toStyledString() << std::endl;
        // This buffer is used for reading and must be persisted
        beast::flat_buffer buffer;

        // Declare a container to hold the response
        http::response<http::dynamic_body> res;

        // Receive the HTTP response
        http::read(stream, buffer, res);

        // Write the message to standard out
        std::cout << "---------------response:\n" << res << std::endl;

        // Gracefully close the socket
        beast::error_code ec;
        stream.socket().shutdown(tcp::socket::shutdown_both, ec);

        // not_connected happens sometimes
        // so don't bother reporting it.
        //
        if (ec && ec != beast::errc::not_connected)
        {
            throw beast::system_error{ec};
        }
    }
    catch (std::exception const& e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
    }
}

void archiveBlocks(auto archiveStorage, auto ledger,
    const std::shared_ptr<bcos::tool::NodeConfig>& nodeConfig, int64_t startBlockNumber,
    int64_t endBlockNumber)
{
    // auto receiptFlag = bcos::ledger::RECEIPTS;
    bcos::crypto::Hash::Ptr hashImpl = nullptr;
    if (nodeConfig->smCryptoType())
    {
        hashImpl = std::make_shared<bcos::crypto::SM3>();
    }
    else
    {
        hashImpl = std::make_shared<::bcos::crypto::Keccak256>();
    }
    auto blockFlag = bcos::ledger::FULL_BLOCK;

    for (int64_t i = startBlockNumber; i < endBlockNumber; ++i)
    {
        // getBlockByNumber
        std::promise<bcos::protocol::Block::Ptr> promise;
        ledger->asyncGetBlockDataByNumber(
            i, blockFlag, [&promise](const Error::Ptr& error, bcos::protocol::Block::Ptr block) {
                if (error)
                {
                    std::cerr << "get block failed: " << error->errorMessage() << endl;
                    exit(1);
                }
                promise.set_value(std::move(block));
            });
        auto block = promise.get_future().get();
        auto size = block->receiptsSize();
        std::vector<std::string> keys(size, "");
        std::vector<std::string> transactionValues(size, "");
        std::vector<std::string> receiptValues(size, "");
        // read the transaction and store to archive database use json format
        tbb::parallel_for(
            tbb::blocked_range<size_t>(0, size), [&](const tbb::blocked_range<size_t>& range) {
                for (size_t j = range.begin(); j < range.end(); ++j)
                {
                    Json::Value value;
                    auto receipt = block->receipt(j);
                    auto transaction = block->transaction(j);
                    auto transactionHash = transaction->hash();
                    keys[j] = std::string((char*)transactionHash.data(), transactionHash.size());
                    // write transactions and receipts to archive database
                    Json::Value transactionJson;
                    bcos::rpc::toJsonResp(transactionJson, transaction);
                    transactionValues[j] = transactionJson.toStyledString();
                    // read the receipt and store to archive database use json format
                    Json::Value receiptJson;
                    bcos::rpc::toJsonResp(receiptJson, toHex(keys[j], "0x"), protocol::TransactionStatus::None,
                        *receipt, nodeConfig->isWasm(), *hashImpl);
                    receiptValues[j] = receiptJson.toStyledString();
                }
            });
        archiveStorage->setRows(ledger::SYS_HASH_2_TX, keys, std::move(transactionValues));
        archiveStorage->setRows(
            ledger::SYS_HASH_2_RECEIPT, std::move(keys), std::move(receiptValues));
        std::cout << "\r"
                  << "write block " << i << " size: " << size << std::flush;
    }
    std::cout << std::endl
              << "write to archive database, block range [" << startBlockNumber << ","
              << endBlockNumber << ")" << std::endl;
    // delete the archived data in node
    std::cout << "delete archived data from " << startBlockNumber << " to " << endBlockNumber
              << endl;
}

void reimportBlocks(auto archiveStorage, TransactionalStorageInterface::Ptr localStorage,
    const std::shared_ptr<bcos::tool::NodeConfig>& nodeConfig, int64_t startBlockNumber,
    int64_t endBlockNumber)
{
    // create factory
    auto protocolInitializer = std::make_shared<ProtocolInitializer>();
    protocolInitializer->init(nodeConfig);
    auto ledger =
        std::make_shared<bcos::ledger::Ledger>(protocolInitializer->blockFactory(), localStorage);
    auto blockFactory = protocolInitializer->blockFactory();
    auto transactionFactory = blockFactory->transactionFactory();
    auto receiptFactory = blockFactory->receiptFactory();

    // get transaction list of block
    // tbb::parallel_for(tbb::blocked_range<size_t>(startBlockNumber, endBlockNumber),
    //     [&](const tbb::blocked_range<size_t>& range) {
    //         for (size_t blockNumber = range.begin(); blockNumber < range.end();
    //         ++blockNumber)
    for (int64_t blockNumber = startBlockNumber; blockNumber < endBlockNumber; ++blockNumber)
    {
        // getBlockByNumber
        std::promise<std::vector<std::string>> promiseHashes;
        ledger->asyncGetBlockTransactionHashes(
            blockNumber, [&](Error::Ptr&& error, std::vector<std::string>&& txHashes) {
                if (error)
                {
                    std::cerr << "get block transaction hash list failed: "
                              << error->errorMessage();
                    exit(1);
                }
                promiseHashes.set_value(std::move(txHashes));
            });
        auto txHashes = promiseHashes.get_future().get();

        // get transactions from archive database
        std::promise<std::vector<std::optional<Entry>>> promiseTxs;
        archiveStorage->asyncGetRows(ledger::SYS_HASH_2_TX, txHashes,
            [&](Error::UniquePtr err, std::vector<std::optional<Entry>> values) {
                if (err)
                {
                    std::cerr << "get transactions failed: " << err->errorMessage();
                    exit(1);
                }
                promiseTxs.set_value(std::move(values));
            });
        auto values = promiseTxs.get_future().get();
        std::vector<bytes> txs(txHashes.size(), bytes());
        std::vector<std::string_view> txsView(txHashes.size(), std::string_view());

        tbb::parallel_for(tbb::blocked_range<size_t>(0, txHashes.size()),
            [&](const tbb::blocked_range<size_t>& range) {
                for (size_t i = range.begin(); i < range.end(); ++i)
                {
                    Json::Value jsonValue;
                    // if value option is none, the data is broken, fatal error
                    if (!values[i])
                    {
                        std::cerr << "get transaction failed, blockNumber: " << blockNumber
                                  << ", txHash: " << toHex(txHashes[i]) << std::endl;
                        exit(1);
                    }
                    auto view = values[i]->get();
                    if (view.empty())
                    {
                        std::cerr << "get transaction empty, blockNumber: " << blockNumber
                                  << ", txHash: " << toHex(txHashes[i]) << std::endl;
                        exit(1);
                    }
                    Json::Reader reader;
                    if (!reader.parse(view.data(), view.data() + view.size(), jsonValue))
                    {
                        std::cerr << "parse json failed: " << reader.getFormattedErrorMessages();
                        exit(1);
                    }
                    // convert json to transaction
                    int32_t version = jsonValue["version"].asInt();
                    auto nonce = u256(jsonValue["nonce"].asString());
                    auto input = fromHexWithPrefix(jsonValue["input"].asString());
                    auto signature = fromHexWithPrefix(jsonValue["signature"].asString());
                    auto tx = transactionFactory->createTransaction(version,
                        jsonValue["to"].asString(), input, nonce, jsonValue["blockLimit"].asInt64(),
                        jsonValue["chainID"].asString(), jsonValue["groupID"].asString(),
                        jsonValue["importTime"].asInt64());
                    dynamic_pointer_cast<bcostars::protocol::TransactionImpl>(tx)->setSignatureData(
                        signature);
                    tx->encode(txs[i]);
                    txsView[i] = std::string_view((char*)txs[i].data(), txs[i].size());
                }
            });

        // write transactions to local storage
        localStorage->setRows(ledger::SYS_HASH_2_TX, txHashes, std::move(txsView));

        // get receipts from archive database
        std::promise<std::vector<std::optional<Entry>>> promiseReceipts;
        archiveStorage->asyncGetRows(ledger::SYS_HASH_2_RECEIPT, txHashes,
            [&](Error::UniquePtr err, std::vector<std::optional<Entry>> values) {
                if (err)
                {
                    std::cerr << "get receipts failed: " << err->errorMessage();
                    exit(1);
                }
                promiseReceipts.set_value(std::move(values));
            });
        values = promiseReceipts.get_future().get();
        std::vector<bytes> receipts(txHashes.size(), bytes());
        std::vector<std::string_view> receiptsView(txHashes.size(), std::string_view());
        tbb::parallel_for(tbb::blocked_range<size_t>(0, txHashes.size()),
            [&](const tbb::blocked_range<size_t>& range) {
                for (size_t i = range.begin(); i < range.end(); ++i)
                {
                    Json::Value jsonValue;
                    Json::Reader reader;
                    // if value option is none, the data is broken, fatal error
                    if (!values[i])
                    {
                        std::cerr << "get receipt failed, blockNumber: " << blockNumber
                                  << ", txHash: " << toHex(txHashes[i]) << std::endl;
                        exit(1);
                    }
                    auto view = values[i]->get();
                    if (view.empty())
                    {
                        std::cerr << "get receipt empty, blockNumber: " << blockNumber
                                  << ", txHash: " << toHex(txHashes[i]) << std::endl;
                        exit(1);
                    }
                    if (!reader.parse(view.data(), view.data() + view.size(), jsonValue))
                    {
                        std::cerr << "parse json failed: " << reader.getFormattedErrorMessages();
                        exit(1);
                    }
                    // convert json to receipt
                    auto gasUsed = u256(jsonValue["gasUsed"].asString());
                    auto status = jsonValue["status"].asInt();
                    bytes output;
                    auto outString = jsonValue["output"].asString();
                    if (outString.size() > 2)
                    {
                        output = fromHexWithPrefix(outString);
                    }
                    std::string contractAddress;
                    if (!nodeConfig->isWasm() && jsonValue["contractAddress"].asString().size() > 2)
                    {
                        auto addressBytes =
                            fromHexWithPrefix(jsonValue["contractAddress"].asString());
                        contractAddress =
                            std::string((char*)addressBytes.data(), addressBytes.size());
                    }
                    std::shared_ptr<std::vector<bcos::protocol::LogEntry>> logEntries =
                        std::make_shared<std::vector<bcos::protocol::LogEntry>>();

                    for (uint j = 0; j < jsonValue["logEntries"].size(); ++j)
                    {
                        auto logEntryJson = jsonValue["logEntries"][j];
                        bcos::protocol::LogEntry logEntry;
                        auto data = fromHexWithPrefix(logEntryJson["data"].asString());
                        h256s topics;
                        for (const auto& k : logEntryJson["topics"])
                        {
                            topics.push_back(h256(k.asString()));
                        }
                        auto address = logEntryJson["address"].asString();
                        auto addr = bytes(address.data(), address.data() + address.size());
                        logEntries->emplace_back(addr, topics, data);
                    }
                    auto receipt = receiptFactory->createReceipt(gasUsed, contractAddress,
                        *logEntries, status, ref(output), jsonValue["blockNumber"].asInt64());
                    receipt->encode(receipts[i]);
                    receiptsView[i] =
                        std::string_view((char*)receipts[i].data(), receipts[i].size());
                }
            });
        // write receipt to local storage
        localStorage->setRows(ledger::SYS_HASH_2_RECEIPT, txHashes, std::move(receiptsView));
        std::cout << "\r"
                  << "reimport block " << blockNumber << " size: " << txHashes.size() << std::flush;
    }
    // });
    std::cout << std::endl
              << "reimport from archive database success, block range [" << startBlockNumber << ","
              << endBlockNumber << ")" << std::endl;
}

int main(int argc, const char* argv[])
{
    boost::property_tree::ptree propertyTree;
    auto params = initCommandLine(argc, argv);
    // parse config file
    std::string configPath("./config.ini");
    if (params.count("config") != 0U)
    {
        configPath = params["config"].as<std::string>();
    }
    if (!fs::exists(configPath))
    {
        cout << "config file not found:" << configPath << endl;
        return 1;
    }

    std::string genesisFilePath("./config.genesis");
    if (params.count("genesis") != 0U)
    {
        genesisFilePath = params["genesis"].as<std::string>();
    }
    cout << "config file: " << configPath << " | genesis file: " << genesisFilePath << endl;
    std::string archivePath;
    if (params.count("path") != 0U)
    {
        archivePath = params["path"].as<std::string>();
    }
    std::string pdAddresses;
    if (params.count("pd") != 0U)
    {
        pdAddresses = params["pd"].as<std::string>();
    }
    std::string endpoint;
    if (params.count("endpoint") != 0U)
    {
        endpoint = params["endpoint"].as<std::string>();
    }
    if (params.count("archive") != 0U && endpoint.empty())
    {
        cout << "the IP::Port of node's archive service is empty" << endl;
        return 1;
    }
    std::vector<std::string> pdAddrs;

    std::string archiveType = "RocksDB";
    if (!archivePath.empty() && pdAddresses.empty())
    {
        cout << "use rocksDB as archive DB, path: " << archivePath << endl;
    }
    else if (archivePath.empty() && !pdAddresses.empty())
    {
        archiveType = "TiKV";
        boost::split(pdAddrs, pdAddresses, boost::is_any_of(","));
        cout << "use TiKV as archive DB, pd address: " << pdAddresses << endl;
    }
    else
    {
        cerr << "please set archive rocksDB path or pd address, not both." << endl;
        return 1;
    }

    bool isArchive = false;
    int64_t startBlockNumber = 0;
    int64_t endBlockNumber = 0;
    if (params.count("archive") != 0U && params.count("reimport") == 0U)
    {
        auto parameters = params["archive"].as<vector<string>>();
        isArchive = true;
        if (parameters.size() != 2)
        {
            cerr << "archive parameters error, should set [start block number] [end block number]"
                 << endl;
            return 1;
        }
        startBlockNumber = std::stoi(parameters[0]);
        endBlockNumber = std::stoi(parameters[1]);
        cout << "archive the blocks of range [" << startBlockNumber << "," << endBlockNumber
             << ") into " << archiveType << endl;
    }
    else if (params.count("reimport") != 0U && params.count("archive") == 0U)
    {
        isArchive = false;
        auto parameters = params["reimport"].as<vector<string>>();
        if (parameters.size() != 2)
        {
            cerr << "reimport parameters error, should set [start block number] [end block number]"
                 << endl;
            return 1;
        }
        startBlockNumber = std::stoi(parameters[0]);
        endBlockNumber = std::stoi(parameters[1]);
        cout << "reimport the blocks of range [" << startBlockNumber << "," << endBlockNumber
             << ") from " << archiveType << endl;
    }
    else
    {
        cerr << "please set archive or reimport, not both." << endl;
        return 1;
    }

    std::string secondaryPath = "./rocksdb_secondary/";

    boost::property_tree::read_ini(configPath, propertyTree);
    auto logInitializer = std::make_shared<BoostLogInitializer>();
    logInitializer->initLog(propertyTree);

    // load node config
    auto keyFactory = std::make_shared<bcos::crypto::KeyFactoryImpl>();
    auto nodeConfig = std::make_shared<bcos::tool::NodeConfig>(keyFactory);
    nodeConfig->loadConfig(configPath);
    if (fs::exists(genesisFilePath))
    {
        nodeConfig->loadGenesisConfig(genesisFilePath);
    }

    // create ledger to get block data
    auto localStorage =
        createBackendStorage(nodeConfig, logInitializer->logPath(), !isArchive, secondaryPath);
    auto protocolInitializer = std::make_shared<ProtocolInitializer>();
    protocolInitializer->init(nodeConfig);
    auto ledger =
        std::make_shared<bcos::ledger::Ledger>(protocolInitializer->blockFactory(), localStorage);
    std::promise<int64_t> promise;
    ledger->asyncGetBlockNumber(
        [&promise](const Error::Ptr& error, bcos::protocol::BlockNumber number) {
            if (error)
            {
                cout << "get block number failed: " << error->errorMessage() << endl;
                exit(1);
            }
            promise.set_value(number);
            cout << "current block number is " << number << endl;
        });
    auto currentBlockNumber = promise.get_future().get();
    // verify the start and end block number, the genesis block should not be archived and the end
    // block number should not be larger than the latest block number
    if (startBlockNumber <= 0 || startBlockNumber >= currentBlockNumber || endBlockNumber <= 0 ||
        endBlockNumber >= currentBlockNumber || startBlockNumber >= endBlockNumber)
    {
        cerr << "invalid block number, start: " << startBlockNumber << ", end: " << endBlockNumber
             << ", current: " << currentBlockNumber << endl;
        return 1;
    }
    // read archived block number to check the request range
    std::promise<std::pair<Error::Ptr, std::optional<bcos::storage::Entry>>> statePromise;
    ledger->asyncGetCurrentStateByKey(ledger::SYS_KEY_ARCHIVED_NUMBER,
        [&statePromise](Error::Ptr&& err, std::optional<bcos::storage::Entry>&& entry) {
            statePromise.set_value(std::make_pair(err, entry));
        });
    auto entry = statePromise.get_future().get().second;
    protocol::BlockNumber archivedBlockNumber = 0;
    if (entry)
    {
        archivedBlockNumber = boost::lexical_cast<protocol::BlockNumber>(entry->get());
    }
    if (isArchive && startBlockNumber < archivedBlockNumber)
    {
        cerr << "the block range [" << startBlockNumber << "," << archivedBlockNumber
             << ") has been archived, the start block number should be " << archivedBlockNumber
             << endl;
        return 1;
    }
    StorageInterface::Ptr archiveStorage = nullptr;
    if (boost::iequals(archiveType, "RocksDB"))
    {  // create archive rocksDB storage
        archiveStorage = StorageInitializer::build(archivePath, nullptr, nodeConfig->keyPageSize());
    }
    else if (boost::iequals(archiveType, "TiKV"))
    {  // create archive TiKV storage
#ifdef WITH_TIKV
        archiveStorage = StorageInitializer::build(pdAddrs, logInitializer->logPath(), "", "", "");
#endif
    }
    else
    {
        std::cerr << "archive storage type not support, only support RocksDB and TiKV, type: "
                  << archiveType << std::endl;
        return 1;
    }
    if (isArchive)
    {
        archiveBlocks(archiveStorage, ledger, nodeConfig, startBlockNumber, endBlockNumber);
        deleteArchivedBlocksInNode(endpoint, startBlockNumber, endBlockNumber);
    }
    else
    {  // reimport
        reimportBlocks(archiveStorage, localStorage, nodeConfig, startBlockNumber, endBlockNumber);
    }
    if (fs::exists(secondaryPath))
    {
        fs::remove_all(secondaryPath);
    }
    return 0;
}
