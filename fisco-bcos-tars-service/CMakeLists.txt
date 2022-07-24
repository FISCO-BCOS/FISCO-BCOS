cmake_minimum_required(VERSION 3.14)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

macro(compile_service SERVICE_SOURCE_PATH BINARY_NAME)
    add_subdirectory(${SERVICE_SOURCE_PATH})

    add_custom_command(OUTPUT ${BINARY_NAME}.tgz
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/fisco-bcos-tars-service/${SERVICE_SOURCE_PATH}/${BINARY_NAME} ${CMAKE_BINARY_DIR}/fisco-bcos-tars-service/${BINARY_NAME}/${BINARY_NAME}
    COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_BINARY_DIR}/fisco-bcos-tars-service tar czfv ${BINARY_NAME}.tgz ${BINARY_NAME}/${BINARY_NAME}
    COMMENT "Compressing ${BINARY_NAME}...")

    add_custom_target(${BINARY_NAME}-tar DEPENDS ${BINARY_NAME}.tgz ${BINARY_NAME})
    list(APPEND SERVICE_TAR_LIST ${BINARY_NAME}-tar)
endmacro()

# RpcService
set(RPC_BINARY_NAME BcosRpcService)
compile_service("RpcService/main" ${RPC_BINARY_NAME})

# GatewayService
set(GATEWAY_BINARY_NAME BcosGatewayService)
compile_service("GatewayService/main" ${GATEWAY_BINARY_NAME})

# pro-NodeService
set(PRO_NODE_SERVICE_BINARY_NAME BcosNodeService)
compile_service("NodeService/pro" ${PRO_NODE_SERVICE_BINARY_NAME})

# max-NodeService
set(MAX_NODE_SERVICE_BINARY_NAME BcosMaxNodeService)
compile_service("NodeService/max" ${MAX_NODE_SERVICE_BINARY_NAME})

# ExecutorService
set(EXECUTOR_BINARY_NAME BcosExecutorService)
compile_service("ExecutorService/main" ${EXECUTOR_BINARY_NAME})

add_custom_target(tar DEPENDS ${SERVICE_TAR_LIST})