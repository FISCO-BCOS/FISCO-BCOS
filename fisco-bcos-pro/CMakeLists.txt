cmake_minimum_required(VERSION 3.15)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/bcos-cmake-scripts)
list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

# FIXME: should move to fisco-bcos-pro or fisco-bcos-max
# import services
list(APPEND SERVICE_LIST GatewayService RpcService NodeService SchedulerService ExecutorService)

foreach(SERVICE ${SERVICE_LIST})
    add_subdirectory(${SERVICE}/main)
    set(BINARY_NAME Bcos${SERVICE})

    add_custom_command(OUTPUT ${BINARY_NAME}.tgz
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/fisco-bcos-pro/${SERVICE}/main/${BINARY_NAME} ${CMAKE_BINARY_DIR}/fisco-bcos-pro/${BINARY_NAME}/${BINARY_NAME}
    COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_BINARY_DIR}/fisco-bcos-pro tar czfv ${BINARY_NAME}.tgz ${BINARY_NAME}/${BINARY_NAME}
    COMMENT "Compressing ${BINARY_NAME}...")

    add_custom_target(${BINARY_NAME}-tar DEPENDS ${BINARY_NAME}.tgz ${BINARY_NAME})
    list(APPEND SERVICE_TAR_LIST ${BINARY_NAME}-tar)
endforeach()

add_custom_target(tar DEPENDS ${SERVICE_TAR_LIST})