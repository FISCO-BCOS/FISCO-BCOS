cmake_minimum_required(VERSION 3.15)
if (NOT DEFINED URL_BASE)
    set(URL_BASE "github.com")
endif()

list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/bcos-cmake-scripts)
list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

include(HunterGate)
HunterGate(
    URL "https://${URL_BASE}/FISCO-BCOS/hunter/archive/bc4754a926acd8ef3b96630c457c75ea4cd55a5d.tar.gz"
    SHA1 "b895385e7ea8312da8db6b1545aa89f11d18345d"
    FILEPATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/config.cmake"
)

project(FISCO-BCOS VERSION "3.0.0")
set(VERSION_SUFFIX "-rc1")

hunter_add_package(Boost COMPONENTS all)
hunter_add_package(tbb)
hunter_add_package(Protobuf)
hunter_add_package(Microsoft.GSL)
hunter_add_package(wedpr-crypto)
hunter_add_package(jsoncpp)
hunter_add_package(OpenSSL)
hunter_add_package(evmc)
hunter_add_package(intx)
hunter_add_package(ethash)
hunter_add_package(evmone)

# import dependencies
include(CompilerSettings)
include(BuildInfoGenerator)

# FIXME: should move to fisco-bcos-pro or fisco-bcos-max
# import services
#list(APPEND SERVICE_LIST FrontService GatewayService RpcService TxPoolService PBFTService NodeService)
# list(APPEND SERVICE_LIST GatewayService RpcService NodeService)
# TODO:
# SchedulerService, ExecutorService

# foreach(SERVICE ${SERVICE_LIST})
#     add_subdirectory(Pro/${SERVICE}/main)
#     set(BINARY_NAME Bcos${SERVICE})

#     add_custom_command(OUTPUT ${BINARY_NAME}.tgz
#     WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
#     COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/${SERVICE}/main/${BINARY_NAME} ${CMAKE_BINARY_DIR}/${BINARY_NAME}/${BINARY_NAME}
#     COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_BINARY_DIR} tar czfv ${BINARY_NAME}.tgz ${BINARY_NAME}/${BINARY_NAME}
#     COMMENT "Compressing ${BINARY_NAME}...")

#     add_custom_target(${BINARY_NAME}-tar DEPENDS ${BINARY_NAME}.tgz ${BINARY_NAME})
#     list(APPEND SERVICE_TAR_LIST ${BINARY_NAME}-tar)
# endforeach()

add_subdirectory(bcos-crypto)
add_subdirectory(bcos-framework)
add_subdirectory(bcos-scheduler)
add_subdirectory(bcos-executor)
add_subdirectory(bcos-storage)
add_subdirectory(bcos-ledger)
add_subdirectory(bcos-tars-protocol)
add_subdirectory(bcos-boostssl)
add_subdirectory(bcos-rpc)
add_subdirectory(bcos-gateway)
add_subdirectory(bcos-pbft)
add_subdirectory(bcos-txpool)
add_subdirectory(bcos-sync)
add_subdirectory(bcos-front)
add_subdirectory(libinitializer)
add_subdirectory(fisco-bcos)
add_subdirectory(fisco-bcos-pro)

# add_custom_target(tar DEPENDS ${SERVICE_TAR_LIST})

if(TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# for code coverage
if (COVERAGE)
    include(Coverage)
    config_coverage("tars-cov" "'/usr*' '${CMAKE_CURRENT_SOURCE_DIR}/bcos-cmake-scripts*' '${CMAKE_CURRENT_SOURCE_DIR}/test/bcos-test*'")
endif ()
