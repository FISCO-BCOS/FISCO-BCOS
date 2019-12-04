/**
 * @CopyRight:
 * FISCO-BCOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FISCO-BCOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2018 fisco-dev contributors.
 *
 * @file storage_main.cpp
 * @author: xingqiangbai
 * @date 2019-10-10
 */

#include "libinitializer/Initializer.h"
#include "libstorage/BinLogHandler.h"
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/program_options.hpp>

using namespace std;
using namespace dev;
using namespace boost;
using namespace dev::storage;
using namespace dev::initializer;
namespace po = boost::program_options;

po::options_description main_options("Main for binlog_reader");

po::variables_map initCommandLine(int argc, const char* argv[])
{
    main_options.add_options()("help,h", "help of binlog_reader")("path,p",
        po::value<string>()->default_value("data/"),
        "[binlog path]")("interval,i", po::value<int64_t>()->default_value(1), "[block interval]")(
        "block,b", po::value<int64_t>()->default_value(-1), "[parse specific block]");
    po::variables_map vm;
    try
    {
        po::store(po::parse_command_line(argc, argv, main_options), vm);
        po::notify(vm);
    }
    catch (...)
    {
        std::cout << "invalid input" << std::endl;
        exit(0);
    }
    /// help information
    if (vm.count("help") || vm.count("h"))
    {
        std::cout << main_options << std::endl;
        exit(0);
    }

    return vm;
}

void help()
{
    std::cout << main_options << std::endl;
}

void printBlockData(const std::vector<TableData::Ptr>& blockData)
{
    auto printEntries = [](Entries::ConstPtr entries) {
        if (entries->size() == 0)
        {
            cout << " is empty!" << endl;
            return;
        }
        for (size_t i = 0; i < entries->size(); ++i)
        {
            auto data = entries->get(i);
            cout << endl << "***" << i << " [id=" << data->getID() << " ]";
            for (auto& it : *data)
            {
                cout << "[ " << it.first << "=" << it.second << " ]";
            }
        }
        cout << endl;
    };

    for (auto tableDataIt : blockData)
    {
        cout << "============================ " << tableDataIt->info->name
             << " key=" << tableDataIt->info->key << " ============================" << endl;
        //  << ", fields" << tableDataIt->info->fields << endl;
        cout << "*** dirtyEntries";
        printEntries(tableDataIt->dirtyEntries);
        cout << "*** newEntries";
        printEntries(tableDataIt->newEntries);
    }
}

int main(int argc, const char* argv[])
{
    // init log
    boost::property_tree::ptree pt;
    auto logInitializer = std::make_shared<LogInitializer>();
    logInitializer->initLog(pt);
    /// init params
    auto params = initCommandLine(argc, argv);
    auto binlogPath = params["path"].as<string>();
    cout << "binlog path : " << binlogPath << endl;
    BinLogHandler binaryLogger(binlogPath);
    int64_t lastBlockNum = binaryLogger.getLastBlockNum();
    int64_t startNum = -1;
    int64_t interval = params["interval"].as<int64_t>();
    int64_t specific = params["block"].as<int64_t>();
    if (specific >= 0)
    {
        auto blocksData = binaryLogger.getMissingBlocksFromBinLog(specific - 1, specific);
        auto blockDataIter = blocksData->find(specific);
        if (blockDataIter == blocksData->end() || blockDataIter->second.empty())
        {
            cout << "block" << specific << "not found, exit";
            return -1;
        }
        else
        {
            const std::vector<TableData::Ptr>& blockData = blockDataIter->second;
            cout << "block" << specific << endl;
            printBlockData(blockData);
        }
        return 0;
    }
    for (int64_t num = startNum; num <= lastBlockNum; num += interval)
    {
        auto blocksData = binaryLogger.getMissingBlocksFromBinLog(num, num + interval);
        if (blocksData->size() > 0)
        {
            for (size_t i = 1; i <= blocksData->size(); ++i)
            {
                auto blockDataIter = blocksData->find(num + i);
                if (blockDataIter == blocksData->end() || blockDataIter->second.empty())
                {
                    cout << "block" << num + i << "not found, exit";
                    return -1;
                }
                else
                {
                    const std::vector<TableData::Ptr>& blockData = blockDataIter->second;
                    cout << "block" << num + i << endl;
                    printBlockData(blockData);
                }
            }
        }
    }
    return 0;
}
