cmake_minimum_required(VERSION 3.15)
if (NOT DEFINED URL_BASE)
    set(URL_BASE "github.com")
endif()

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/bcos-cmake-scripts)

include(HunterGate)
HunterGate(
    URL "https://${URL_BASE}/FISCO-BCOS/hunter/archive/203039fdd0826330facd9c3841082dbf35d0c808.tar.gz"
    SHA1 "26ca1840eb26da410913469f2f278569c8d383ae"
    FILEPATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/config.cmake"
)

project(FISCO-BCOS VERSION "3.0.0")
set(VERSION_SUFFIX "-rc1")

# import dependencies
include(InstallBoostSSL)
include(CompilerSettings)

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
# add_subdirectory(libinitializer)
# add_subdirectory(fisco-bcos)

# add_custom_target(tar DEPENDS ${SERVICE_TAR_LIST})

# if(TEST)
#     enable_testing()
#     add_subdirectory(tests)
# endif()

# for code coverage
if (COVERAGE)
    include(Coverage)
    config_coverage("tars-cov" "'/usr*' '${CMAKE_CURRENT_SOURCE_DIR}/bcos-cmake-scripts*' '${CMAKE_CURRENT_SOURCE_DIR}/test/bcos-test*'")
endif ()
