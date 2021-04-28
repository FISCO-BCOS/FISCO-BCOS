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

add_executable(fisco-bcos main.cpp ${HEADERS})
target_include_directories(fisco-bcos PRIVATE ..)
target_link_libraries(fisco-bcos PUBLIC initializer)

install(TARGETS fisco-bcos DESTINATION bin)

if (TOOL)
    add_executable(fisco-sync DataSyncTool.cpp)
    target_include_directories(fisco-sync PUBLIC ..)
    target_link_libraries(fisco-sync PUBLIC initializer storage)
    install(TARGETS fisco-sync DESTINATION bin)
endif()
