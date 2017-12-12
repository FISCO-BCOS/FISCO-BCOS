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
 * @file: InterfaceStatistics.h
 * @author: fisco-dev
 * 
 * @date: 2017
 */

#ifndef DEV_INTERFACE_STATISTICS_H_
#define DEV_INTERFACE_STATISTICS_H_

#include <string>
#include <thread>
#include <memory>
#include <mutex>
#include <chrono>
#include "boost_shm.h"

#define IS_MAPPED_FILE_NAME "_MappedFile"
#define IS_MAPPED_FILE_SIZE 1048576
#define IS_SHM_HASH_MAP_ARRAY "shm_hash_map_array"
#define IS_SHM_TIME_POINT_ARRAY "shm_time_point_array"
#define IS_SHM_VARS 2
#define IS_SHM_GROW_SIZE 102400

namespace dev
{

using time_point = std::chrono::system_clock::time_point;
struct interfaceInfo
{
  int count;
  // milliseconds
  int totalTime;
  int maxTime;
  interfaceInfo() : count(0), totalTime(0), maxTime(0) {}
};

// ShmHashMap
typedef ShmString KeyType;
typedef interfaceInfo MappedType;
typedef std::pair<const KeyType, MappedType> ValueType;
typedef boost::interprocess::allocator<ValueType, SegmentManager> ShmemAllocator;
typedef boost::unordered_map<KeyType, MappedType, boost::hash<KeyType>, std::equal_to<KeyType>, ShmemAllocator> ShmHashMap;

class InterfaceStatistics
{
public:
  explicit InterfaceStatistics(const std::string &_moduleName, const int &interval = 0);
  ~InterfaceStatistics();
  void interfaceCalled(const std::string &key, const int &timeUsed = 0);

private:
  void init();
  void checkShm();
  std::shared_ptr<std::thread> writeThread;
  std::string moduleName;
  std::string shmName;
  // seconds to write
  int writeInterval;
  std::mutex mt;
  bool shutdownThread;
  int turn;
  boost::interprocess::managed_mapped_file *mappedFile;
  time_point *timeArray;
  ShmHashMap *shmHashMapArray;
  void writeLog(int i);
};
}

#endif //DEV_INTERFACE_STATISTICS_H_
