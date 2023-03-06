#------------------------------------------------------------------------------
# Top-level CMake file for bcos-framework
# ------------------------------------------------------------------------------
# Copyright (C) 2021 FISCO BCOS.
# SPDX-License-Identifier: Apache-2.0
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
# ------------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.12)
set(CMAKE_OSX_DEPLOYMENT_TARGET "12" CACHE STRING "Minimum OS X deployment version")

include_directories(${CMAKE_CURRENT_BINARY_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/bcos-framework)

find_package(Microsoft.GSL CONFIG REQUIRED)

add_library(bcos-framework INTERFACE)
target_include_directories(bcos-framework INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include/bcos-framework>)
target_link_libraries(bcos-framework INTERFACE bcos-crypto bcos-task Microsoft.GSL::GSL)
if (PRECOMPILED_HEADER)
    target_precompile_headers(bcos-framework
    INTERFACE 
        ${CMAKE_CURRENT_SOURCE_DIR}/bcos-framework/Common.h
        ${CMAKE_CURRENT_SOURCE_DIR}/bcos-framework/consensus/ConsensusInterface.h
        ${CMAKE_CURRENT_SOURCE_DIR}/bcos-framework/consensus/ConsensusNode.h
        ${CMAKE_CURRENT_SOURCE_DIR}/bcos-framework/consensus/ConsensusNodeInterface.h
        ${CMAKE_CURRENT_SOURCE_DIR}/bcos-framework/consensus/ConsensusTypeDef.h
        ${CMAKE_CURRENT_SOURCE_DIR}/bcos-framework/dispatcher/SchedulerInterface.h
        ${CMAKE_CURRENT_SOURCE_DIR}/bcos-framework/dispatcher/SchedulerTypeDef.h
        ${CMAKE_CURRENT_SOURCE_DIR}/bcos-framework/election/FailOverTypeDef.h
        ${CMAKE_CURRENT_SOURCE_DIR}/bcos-framework/election/LeaderElectionInterface.h
        ${CMAKE_CURRENT_SOURCE_DIR}/bcos-framework/election/LeaderEntryPointInterface.h
        ${CMAKE_CURRENT_SOURCE_DIR}/bcos-framework/executor/ExecuteError.h
        ${CMAKE_CURRENT_SOURCE_DIR}/bcos-framework/executor/ExecutionMessage.h
        ${CMAKE_CURRENT_SOURCE_DIR}/bcos-framework/executor/ExecutorStatus.h
        ${CMAKE_CURRENT_SOURCE_DIR}/bcos-framework/executor/NativeExecutionMessage.h
        ${CMAKE_CURRENT_SOURCE_DIR}/bcos-framework/executor/ParallelTransactionExecutorInterface.h
        ${CMAKE_CURRENT_SOURCE_DIR}/bcos-framework/executor/PrecompiledTypeDef.h
        ${CMAKE_CURRENT_SOURCE_DIR}/bcos-framework/front/FrontServiceInterface.h
        ${CMAKE_CURRENT_SOURCE_DIR}/bcos-framework/gateway/GatewayInterface.h
        ${CMAKE_CURRENT_SOURCE_DIR}/bcos-framework/gateway/GatewayTypeDef.h
        ${CMAKE_CURRENT_SOURCE_DIR}/bcos-framework/gateway/GroupNodeInfo.h
        ${CMAKE_CURRENT_SOURCE_DIR}/bcos-framework/ledger/GenesisConfig.h
        ${CMAKE_CURRENT_SOURCE_DIR}/bcos-framework/ledger/LedgerConfig.h
        ${CMAKE_CURRENT_SOURCE_DIR}/bcos-framework/ledger/LedgerInterface.h
        ${CMAKE_CURRENT_SOURCE_DIR}/bcos-framework/ledger/LedgerTypeDef.h
        ${CMAKE_CURRENT_SOURCE_DIR}/bcos-framework/multigroup/ChainNodeInfo.h
        ${CMAKE_CURRENT_SOURCE_DIR}/bcos-framework/multigroup/ChainNodeInfoFactory.h
        ${CMAKE_CURRENT_SOURCE_DIR}/bcos-framework/multigroup/GroupInfo.h
        ${CMAKE_CURRENT_SOURCE_DIR}/bcos-framework/multigroup/GroupInfoCodec.h
        ${CMAKE_CURRENT_SOURCE_DIR}/bcos-framework/multigroup/GroupInfoFactory.h
        ${CMAKE_CURRENT_SOURCE_DIR}/bcos-framework/multigroup/GroupTypeDef.h
        ${CMAKE_CURRENT_SOURCE_DIR}/bcos-framework/protocol/Block.h
        ${CMAKE_CURRENT_SOURCE_DIR}/bcos-framework/protocol/BlockFactory.h
        ${CMAKE_CURRENT_SOURCE_DIR}/bcos-framework/protocol/BlockHeader.h
        ${CMAKE_CURRENT_SOURCE_DIR}/bcos-framework/protocol/BlockHeaderFactory.h
        ${CMAKE_CURRENT_SOURCE_DIR}/bcos-framework/protocol/CommonError.h
        ${CMAKE_CURRENT_SOURCE_DIR}/bcos-framework/protocol/Exceptions.h
        ${CMAKE_CURRENT_SOURCE_DIR}/bcos-framework/protocol/GlobalConfig.h
        ${CMAKE_CURRENT_SOURCE_DIR}/bcos-framework/protocol/LogEntry.h
        ${CMAKE_CURRENT_SOURCE_DIR}/bcos-framework/protocol/MemberInterface.h
        ${CMAKE_CURRENT_SOURCE_DIR}/bcos-framework/protocol/Protocol.h
        ${CMAKE_CURRENT_SOURCE_DIR}/bcos-framework/protocol/ProtocolInfo.h
        ${CMAKE_CURRENT_SOURCE_DIR}/bcos-framework/protocol/ProtocolInfoCodec.h
        ${CMAKE_CURRENT_SOURCE_DIR}/bcos-framework/protocol/ProtocolTypeDef.h
        ${CMAKE_CURRENT_SOURCE_DIR}/bcos-framework/protocol/ServiceDesc.h
        ${CMAKE_CURRENT_SOURCE_DIR}/bcos-framework/protocol/Transaction.h
        ${CMAKE_CURRENT_SOURCE_DIR}/bcos-framework/protocol/TransactionFactory.h
        ${CMAKE_CURRENT_SOURCE_DIR}/bcos-framework/protocol/TransactionMetaData.h
        ${CMAKE_CURRENT_SOURCE_DIR}/bcos-framework/protocol/TransactionReceipt.h
        ${CMAKE_CURRENT_SOURCE_DIR}/bcos-framework/protocol/TransactionReceiptFactory.h
        ${CMAKE_CURRENT_SOURCE_DIR}/bcos-framework/protocol/TransactionSubmitResult.h
        ${CMAKE_CURRENT_SOURCE_DIR}/bcos-framework/protocol/TransactionSubmitResultFactory.h
        ${CMAKE_CURRENT_SOURCE_DIR}/bcos-framework/rpc/HandshakeRequest.h
        ${CMAKE_CURRENT_SOURCE_DIR}/bcos-framework/rpc/RPCInterface.h
        ${CMAKE_CURRENT_SOURCE_DIR}/bcos-framework/sealer/SealerInterface.h
        ${CMAKE_CURRENT_SOURCE_DIR}/bcos-framework/security/DataEncryptInterface.h
        ${CMAKE_CURRENT_SOURCE_DIR}/bcos-framework/storage/Common.h
        ${CMAKE_CURRENT_SOURCE_DIR}/bcos-framework/storage/Entry.h
        ${CMAKE_CURRENT_SOURCE_DIR}/bcos-framework/storage/KVStorageHelper.h
        ${CMAKE_CURRENT_SOURCE_DIR}/bcos-framework/storage/StorageInterface.h
        ${CMAKE_CURRENT_SOURCE_DIR}/bcos-framework/storage/Table.h
        ${CMAKE_CURRENT_SOURCE_DIR}/bcos-framework/storage2/MemoryStorage.h
        ${CMAKE_CURRENT_SOURCE_DIR}/bcos-framework/storage2/Storage.h
        ${CMAKE_CURRENT_SOURCE_DIR}/bcos-framework/storage2/StringPool.h
        ${CMAKE_CURRENT_SOURCE_DIR}/bcos-framework/sync/BlockSyncInterface.h
        ${CMAKE_CURRENT_SOURCE_DIR}/bcos-framework/sync/SyncConfig.h
        ${CMAKE_CURRENT_SOURCE_DIR}/bcos-framework/transaction-executor/TransactionExecutor.h
        ${CMAKE_CURRENT_SOURCE_DIR}/bcos-framework/transaction-scheduler/TransactionScheduler.h
        ${CMAKE_CURRENT_SOURCE_DIR}/bcos-framework/txpool/TxPoolInterface.h
        ${CMAKE_CURRENT_SOURCE_DIR}/bcos-framework/txpool/TxPoolTypeDef.h
    )
endif()

if (TESTS)
    enable_testing()
    set(CTEST_OUTPUT_ON_FAILURE TRUE)
    add_subdirectory(test)
    add_subdirectory(benchmark)
endif()

# for code coverage
if (COVERAGE)
    include(Coverage)
    config_coverage("framework-cov" "'/usr*' '${CMAKE_CURRENT_SOURCE_DIR}/bcos-cmake-scripts*' '${CMAKE_CURRENT_SOURCE_DIR}/test/bcos-test*'")
endif ()

include(GNUInstallDirs)
install(TARGETS bcos-framework EXPORT fiscobcosTargets ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(DIRECTORY "bcos-framework" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}" FILES_MATCHING PATTERN "*.h")