cmake_minimum_required(VERSION 3.15)
if (NOT DEFINED URL_BASE)
    set(URL_BASE "github.com")
    # set(URL_BASE "github.com.cnpmjs.org")
endif()
set (DEPENDENCIES_DIR ${CMAKE_CURRENT_SOURCE_DIR}/deps/src)

set(BCOS_TARS_CMAKE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/cmake" CACHE PATH "The cmake path for the library")
set(BCOS_CMAKE_SCRIPTS_DIR ${CMAKE_CURRENT_BINARY_DIR}/bcos-cmake-scripts)
include(FetchContent)
FetchContent_Declare(bcos-cmake-scripts
    GIT_REPOSITORY https://${URL_BASE}/FISCO-BCOS/bcos-cmake-scripts.git
    GIT_TAG 397f904f335bdc2be909565edad4545e2e5fa876
    SOURCE_DIR ${BCOS_CMAKE_SCRIPTS_DIR}
)
FetchContent_MakeAvailable(bcos-cmake-scripts)
list(APPEND CMAKE_MODULE_PATH ${BCOS_CMAKE_SCRIPTS_DIR})
list(APPEND CMAKE_MODULE_PATH ${BCOS_TARS_CMAKE_DIR})

include(HunterGate)
HunterGate(
    URL "https://${URL_BASE}/FISCO-BCOS/hunter/archive/b55a347258b0fc3eda47f4ccd3e2c0db595b4957.tar.gz"
    SHA1 "9331d1e70cee5498bd59ada13aa68278cbb9a2d6"
    FILEPATH "${BCOS_TARS_CMAKE_DIR}/config.cmake"
)

project(FISCO-BCOS VERSION "3.0.0")
# Suffix like "-rc1" e.t.c. to append to versions wherever needed.
set(VERSION_SUFFIX "-rc1")

# import dependencies
include(InstallBcosFrameworkDependencies)
include(InstallBcosCryptoDependencies)
include(InstallBcosTarsProtocolDependencies)
include(InstallBoostSSL)

include(InstallBcosGateWayDependencies)
include(InstallBcosRpcDependencies)

include(InstallBcosTxPoolDependencies)
include(InstallBcosPBFTDependencies)
include(InstallBcosStorageDependencies)
include(InstallBcosLedgerDependencies)
include(InstallBcosSyncDependencies)
include(InstallBcosFrontDependencies)
include(InstallBcosScheduler)
include(ProjectBCOSExecutor)

# copy .clang-format into the project path
include(CopyClangFormat)

include_directories(${CMAKE_INSTALL_INCLUDEDIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${CMAKE_CURRENT_BINARY_DIR})

include(CompilerSettings)

set(INIT_LIB initializer)
set(PROTOCOL_INIT_LIB protocolInitializer)
set(PBFT_INIT_LIB pbftInitializer)
set(TXPOOL_INIT_LIB txPoolInitializer)
set(FRONTSERVICE_INIT_LIB frontInitializer)

set(LEDGER_SERVICE_LIB ledgerServiceLib)
set(SCHEDULER_SERVICE_LIB schedulerServiceLib)

add_subdirectory(libinitializer)

include(BuildInfoGenerator)
# import services
#list(APPEND SERVICE_LIST FrontService GatewayService RpcService TxPoolService PBFTService NodeService)
list(APPEND SERVICE_LIST GatewayService RpcService NodeService)
# TODO:
# SchedulerService, ExecutorService

foreach(SERVICE ${SERVICE_LIST})
    add_subdirectory(${SERVICE}/main)
    set(BINARY_NAME Bcos${SERVICE})

    add_custom_command(OUTPUT ${BINARY_NAME}.tgz
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/${SERVICE}/main/${BINARY_NAME} ${CMAKE_BINARY_DIR}/${BINARY_NAME}/${BINARY_NAME}
    COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_BINARY_DIR} tar czfv ${BINARY_NAME}.tgz ${BINARY_NAME}/${BINARY_NAME}                              
    COMMENT "Compressing ${BINARY_NAME}...")

    add_custom_target(${BINARY_NAME}-tar DEPENDS ${BINARY_NAME}.tgz ${BINARY_NAME})
    list(APPEND SERVICE_TAR_LIST ${BINARY_NAME}-tar)
endforeach()

add_subdirectory(fisco-bcos)
add_custom_target(tar DEPENDS ${SERVICE_TAR_LIST})

if(TEST)
    enable_testing()
    add_subdirectory(tests)
endif()

# for code coverage
if (COVERAGE)
    include(Coverage)
    config_coverage("tars-cov" "'/usr*' '${CMAKE_CURRENT_SOURCE_DIR}/bcos-cmake-scripts*' '${CMAKE_CURRENT_SOURCE_DIR}/test/bcos-test*'")
endif ()
