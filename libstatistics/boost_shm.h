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
 * @file: boost_shm.h
 * @author: fisco-dev
 * 
 * @date: 2017
 */

#ifndef DEV_BOOST_SHM_H
#define DEV_BOOST_SHM_H
// #include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/managed_mapped_file.hpp>
#include <boost/interprocess/allocators/allocator.hpp>
#include <boost/interprocess/containers/string.hpp>

#include <boost/unordered_map.hpp>   //boost::unordered_map
#include <functional>                //std::equal_to hash
#include <boost/functional/hash.hpp> //boost::hash

//Typedefs
typedef boost::interprocess::managed_mapped_file::segment_manager SegmentManager;
typedef boost::interprocess::allocator<char, SegmentManager> ShmCharAllocator;
typedef boost::interprocess::basic_string<char, std::char_traits<char>, ShmCharAllocator> ShmString;
typedef boost::interprocess::allocator<ShmString, SegmentManager> ShmStringAllocator;

#endif //DEV_SHM_UNORDERED_MAP_H