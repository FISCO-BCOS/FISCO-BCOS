/*
	This file is part of cpp-ethereum.

	cpp-ethereum is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	cpp-ethereum is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with cpp-ethereum.  If not, see <http://www.gnu.org/licenses/>.
*/
/**
 * @file: InterfaceStatistics.cpp
 * @author: fisco-dev
 * 
 * @date: 2017
 */

#include "InterfaceStatistics.h"
#include "libdevcore/easylog.h"
#include <ctime>
#include <iomanip>

using namespace std;
using namespace dev;
using namespace boost::interprocess;

InterfaceStatistics::InterfaceStatistics(const std::string &_moduleName, const int &interval) : writeThread(nullptr), moduleName(_moduleName),
                                                                                                writeInterval(interval), shutdownThread(false), turn(0), mappedFile(nullptr)
{
    if (!moduleName.empty() && writeInterval > 0)
    {
        shmName = moduleName + IS_MAPPED_FILE_NAME;
        auto i = moduleName.find_last_of("/") == std::string::npos ? 0 : moduleName.find_last_of("/") + 1;
        moduleName = moduleName.substr(i);
        if (moduleName.size() > 8)
            moduleName = moduleName.substr(0, 8);
        checkShm();
        init();
    }
    else
    {
        LOG(DEBUG) << moduleName << " 's statistics is off";
    }
}

void InterfaceStatistics::checkShm()
{
    int freeSize = 0;
    int shmSize = 0;
    try
    {
        managed_mapped_file mfile(open_or_create, shmName.c_str(), IS_MAPPED_FILE_SIZE);
        freeSize = mfile.get_free_memory();
        shmSize = mfile.get_size();
    }
    catch (interprocess_exception &e)
    {
        LOG(ERROR) << e.what();
    }
    if (freeSize < IS_SHM_GROW_SIZE)
    {
        managed_mapped_file::grow(shmName.c_str(), shmSize);
    }
}

void InterfaceStatistics::init()
{
    try
    {
        mappedFile = new managed_mapped_file(open_or_create, shmName.c_str(), IS_MAPPED_FILE_SIZE);
    }
    catch (interprocess_exception &e)
    {
        LOG(ERROR) << e.what();
    }
    shmHashMapArray = mappedFile->find_or_construct<ShmHashMap>(IS_SHM_HASH_MAP_ARRAY)[IS_SHM_VARS] //object name
                      (100, boost::hash<KeyType>(), std::equal_to<KeyType>()                        //
                       ,
                       mappedFile->get_allocator<ValueType>()); //allocator instance
    timeArray = mappedFile->find_or_construct<time_point>(IS_SHM_TIME_POINT_ARRAY)[IS_SHM_VARS](chrono::system_clock::now());
    // deal after restart
    for (int i = 0; i < IS_SHM_VARS; ++i)
        writeLog(i);

    writeThread = make_shared<thread>(thread([&]() {
        while (!shutdownThread)
        {
            this_thread::sleep_for(chrono::seconds(writeInterval));
            int toWrite = turn;
            {
                lock_guard<std::mutex> l(mt);
                turn = !turn;
                //update time
                if (!shutdownThread)
                    timeArray[turn] = chrono::system_clock::now();
            }
            writeLog(toWrite);
        }
    }));
}

void InterfaceStatistics::interfaceCalled(const std::string &key, const int &timeUsed)
{
    if (writeInterval <= 0)
        return;
    lock_guard<std::mutex> l(mt);
    ShmString shmString(key.c_str(), ShmCharAllocator(mappedFile->get_segment_manager()));
    auto got = shmHashMapArray[turn].find(shmString);
    if (got == shmHashMapArray[turn].end())
    {
        interfaceInfo info;
        info.count++;
        info.totalTime += timeUsed;
        info.maxTime = max(timeUsed, info.maxTime);
        shmHashMapArray[turn].insert(ValueType(ShmString(key.c_str(), mappedFile->get_segment_manager()), info));
    }
    else
    {
        got->second.count++;
        got->second.totalTime += timeUsed;
        got->second.maxTime = max(timeUsed, got->second.maxTime);
    }
}

void InterfaceStatistics::writeLog(int i)
{
    //write log and truncate hashmap
    if (!shmHashMapArray[i].empty())
    {
        auto t = chrono::system_clock::to_time_t(timeArray[i]);
        for (auto item : shmHashMapArray[i])
        {
#if defined(__GNUC__) && __GNUC__ > 5
            LOG(INFO) << std::put_time(std::localtime(&t), "%H:%M:%S") << left << setfill(' ')
                      << "|Statistics " << moduleName << "|Name:" << setw(25) << item.first
                      << "|Count: " << right << setw(3) << item.second.count
                      << "|Avg: " << fixed << setprecision(2) << item.second.totalTime / (float)item.second.count
                      << " ms|Max: " << setw(2) << item.second.maxTime << " ms" << endl;
#else
            char timeStr[10];
            strftime(timeStr, sizeof(timeStr), "%H:%M:%S", std::localtime(&t));
            LOG(INFO) << timeStr << left << setfill(' ')
                      << "|Statistics " << moduleName << "|Name:" << setw(25) << item.first
                      << "|Count: " << right << setw(3) << item.second.count
                      << "|Avg: " << fixed << setprecision(2) << item.second.totalTime / (float)item.second.count
                      << " ms|Max: " << setw(2) << item.second.maxTime << " ms" << endl;
#endif
        }
        timeArray[i] = chrono::system_clock::now();
        shmHashMapArray[i].clear();
    }
}

InterfaceStatistics::~InterfaceStatistics()
{
    if (writeInterval > 0)
    {
        shutdownThread = true;
        writeThread->join();
        delete mappedFile;
        boost::interprocess::file_mapping::remove(shmName.c_str());
    }
}
