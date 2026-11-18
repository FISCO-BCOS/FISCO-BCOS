
list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

macro(compile_service SERVICE_SOURCE_PATH BINARY_NAME INCLUDES_RPC)
    add_subdirectory(${SERVICE_SOURCE_PATH})

    set(package_members ${BINARY_NAME}/${BINARY_NAME})
    set(package_dependencies ${CMAKE_BINARY_DIR}/fisco-bcos-tars-service/${SERVICE_SOURCE_PATH}/${BINARY_NAME})
    set(copy_license)
    if (${INCLUDES_RPC})
        set(valijson_license ${CMAKE_SOURCE_DIR}/ports/valijson/LICENSE)
        set(copy_license COMMAND ${CMAKE_COMMAND} -E copy "${valijson_license}"
            "${CMAKE_CURRENT_BINARY_DIR}/${BINARY_NAME}/valijson.LICENSE")
        list(APPEND package_members ${BINARY_NAME}/valijson.LICENSE)
        list(APPEND package_dependencies "${valijson_license}")
    endif()
    add_custom_command(OUTPUT ${BINARY_NAME}.tgz
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/fisco-bcos-tars-service/${SERVICE_SOURCE_PATH}/${BINARY_NAME} ${CMAKE_BINARY_DIR}/fisco-bcos-tars-service/${BINARY_NAME}/${BINARY_NAME}
    ${copy_license}
    COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_BINARY_DIR}/fisco-bcos-tars-service tar czfv ${BINARY_NAME}.tgz ${package_members}
    COMMENT "Compressing ${BINARY_NAME}..." DEPENDS ${package_dependencies})

    add_custom_target(${BINARY_NAME}-tar DEPENDS ${BINARY_NAME}.tgz ${BINARY_NAME})
    list(APPEND SERVICE_TAR_LIST ${BINARY_NAME}-tar)
endmacro()

# RpcService
set(RPC_BINARY_NAME BcosRpcService)
compile_service("RpcService/main" ${RPC_BINARY_NAME} ON)

# GatewayService
set(GATEWAY_BINARY_NAME BcosGatewayService)
compile_service("GatewayService/main" ${GATEWAY_BINARY_NAME} OFF)

# pro-NodeService
set(PRO_NODE_SERVICE_BINARY_NAME BcosNodeService)
compile_service("NodeService/pro" ${PRO_NODE_SERVICE_BINARY_NAME} ON)

# max-NodeService
set(MAX_NODE_SERVICE_BINARY_NAME BcosMaxNodeService)
compile_service("NodeService/max" ${MAX_NODE_SERVICE_BINARY_NAME} ON)

# ExecutorService (TiKV-based, deprecated) has been archived to
# legacy/ExecutorService and is no longer built.

add_custom_target(tar DEPENDS ${SERVICE_TAR_LIST})
