file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

set(BINARY_NAME fisco-bcos)
add_executable(${BINARY_NAME} ${SRC_LIST} ${HEADERS})

list(APPEND InitLibs ${INIT_LIB} bcos-rpc::rpc bcos-gateway::bcos-gateway tarscpp::tarsservant tarscpp::tarsutil)

target_compile_options(${BINARY_NAME} PRIVATE -Wno-error -Wno-unused-parameter -Wno-variadic-macros -Wno-return-type -Wno-pedantic)

target_link_libraries(${BINARY_NAME} PUBLIC ${InitLibs})