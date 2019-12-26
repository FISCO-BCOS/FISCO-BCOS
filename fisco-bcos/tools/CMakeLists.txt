#------------------------------------------------------------------------------
# Link libraries into main.cpp to generate executable binrary fisco-bcos
# ------------------------------------------------------------------------------
# This file is part of FISCO-BCOS.
#
# FISCO-BCOS is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# FISCO-BCOS is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
#
# (c) 2016-2018 fisco-dev contributors.
#------------------------------------------------------------------------------

file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

if (TOOL)
    add_executable(leveldb-storage storage_main.cpp)
    target_link_libraries(leveldb-storage PUBLIC initializer)

    add_executable(binlog_reader binlog_reader.cpp)
    target_link_libraries(binlog_reader PUBLIC initializer)

    add_executable(rocksdb-storage rocksdb_main.cpp)
    target_link_libraries(rocksdb-storage PUBLIC initializer storage)
    add_executable(calculate_address calculate_address.cpp)
    target_link_libraries(calculate_address PUBLIC devcrypto devcore)
endif()