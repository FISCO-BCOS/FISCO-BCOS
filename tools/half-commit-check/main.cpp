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
 * @file main.cpp
 * @brief offline half-commit diagnostic CLI (spec §5.12) — thin shell over the
 *        halfcommit-checker library; node must be stopped before running
 */
#include "bcos-ledger/halfcommit/HalfCommitChecker.h"
#include "bcos-tars-protocol/protocol/BlockFactoryImpl.h"
#include "bcos-tars-protocol/protocol/BlockHeaderFactoryImpl.h"
#include "bcos-tars-protocol/protocol/TransactionFactoryImpl.h"
#include "bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <rocksdb/db.h>
#include <boost/program_options.hpp>
#include <fstream>
#include <iostream>
#include <memory>

namespace
{

// Exit codes (consumed by ops runbooks — keep stable):
// 0 = scan OK (orphans, if any, are listed in the report)
// 1 = DB corruption detected
// 2 = CLI argument error
// 3 = failed to open RocksDB
// 4 = failed to open the --dump-orphans output file
enum ExitCode : int
{
    EXIT_SCAN_OK = 0,
    EXIT_DB_CORRUPTED = 1,
    EXIT_BAD_ARGUMENTS = 2,
    EXIT_DB_OPEN_FAILED = 3,
    EXIT_DUMP_FILE_FAILED = 4,
};

bcos::protocol::BlockFactory::Ptr buildBlockFactory()
{
    // Only used to decode transaction-metadata blocks (stored hashes are read
    // verbatim, nothing is re-hashed), so the concrete hash/signature scheme
    // of the chain does not matter here.
    auto cryptoSuite = std::make_shared<bcos::crypto::CryptoSuite>(
        std::make_shared<bcos::crypto::Keccak256>(), nullptr, nullptr);
    auto blockHeaderFactory =
        std::make_shared<bcostars::protocol::BlockHeaderFactoryImpl>(cryptoSuite);
    auto transactionFactory =
        std::make_shared<bcostars::protocol::TransactionFactoryImpl>(cryptoSuite);
    auto receiptFactory =
        std::make_shared<bcostars::protocol::TransactionReceiptFactoryImpl>(cryptoSuite);
    return std::make_shared<bcostars::protocol::BlockFactoryImpl>(
        cryptoSuite, blockHeaderFactory, transactionFactory, receiptFactory);
}

}  // namespace

int main(int argc, char* argv[])
{
    namespace po = boost::program_options;
    po::options_description description(
        "half-commit-check — offline scan for blocks whose tx/nonce index rows were persisted "
        "but whose state commit never advanced current_number (spec §5.12).\n"
        "The node must be STOPPED; the database is opened read-only.\n\nOptions");
    description.add_options()("help,h", "print this help")("datadir,d", po::value<std::string>(),
        "node state RocksDB directory (data/group0... in AIR)")("report-only", po::bool_switch(),
        "print the diagnostic report only (default behavior)")("dump-orphans",
        po::value<std::string>(),
        "additionally write orphan tx hashes to <file>, one '<blockNumber> <txHash>' per line");

    po::variables_map variables;
    try
    {
        po::store(po::parse_command_line(argc, argv, description), variables);
        po::notify(variables);
    }
    catch (std::exception const& e)
    {
        std::cout << "Error: " << e.what() << "\n" << description << "\n";
        return EXIT_BAD_ARGUMENTS;
    }
    if (variables.count("help") != 0)
    {
        std::cout << description << "\n";
        return EXIT_SCAN_OK;
    }
    if (variables.count("datadir") == 0)
    {
        std::cout << "Error: --datadir is required\n" << description << "\n";
        return EXIT_BAD_ARGUMENTS;
    }
    if (variables["report-only"].as<bool>() && variables.count("dump-orphans") != 0)
    {
        std::cout << "Error: --report-only and --dump-orphans are mutually exclusive\n";
        return EXIT_BAD_ARGUMENTS;
    }

    auto datadir = variables["datadir"].as<std::string>();
    ::rocksdb::Options options;
    options.create_if_missing = false;
    ::rocksdb::DB* rawDB = nullptr;
    auto status = ::rocksdb::DB::OpenForReadOnly(options, datadir, &rawDB);
    if (!status.ok())
    {
        std::cout << "Error: failed to open RocksDB at " << datadir
                  << " read-only: " << status.ToString() << "\n";
        return EXIT_DB_OPEN_FAILED;
    }
    std::unique_ptr<::rocksdb::DB> database(rawDB);

    auto blockFactory = buildBlockFactory();
    auto report = bcos::ledger::halfcommit::scan(*database, *blockFactory);
    std::cout << bcos::ledger::halfcommit::formatReport(report);
    if (report.dbCorrupted)
    {
        return EXIT_DB_CORRUPTED;
    }

    if (variables.count("dump-orphans") != 0)
    {
        auto dumpPath = variables["dump-orphans"].as<std::string>();
        std::ofstream dumpFile(dumpPath, std::ios::trunc);
        if (!dumpFile)
        {
            std::cout << "Error: failed to open dump file " << dumpPath << "\n";
            return EXIT_DUMP_FILE_FAILED;
        }
        bcos::ledger::halfcommit::dumpOrphans(report, dumpFile);
        std::cout << "Dumped " << report.orphans.size() << " orphan blocks' tx hashes to "
                  << dumpPath << "\n";
    }
    return EXIT_SCAN_OK;
}
