# bcos-console: Built-in interactive console for FISCO BCOS
# Depends on: bcos-rpc, bcos-crypto, bcos-boostssl, bcos-utilities, tomlplusplus

find_package(tomlplusplus CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)

# Detect solc binary at configure time
find_program(SOLC_EXECUTABLE solc)
if(NOT SOLC_EXECUTABLE)
    message(WARNING "solc not found — 'deploy' command will show an install hint. "
                    "Install with: apt install solc or download from https://github.com/ethereum/solidity")
endif()

add_library(bcos-console
    ConsoleApp.cpp
    ConsoleRepl.cpp
    CommandDispatcher.cpp
    OutputFormatter.cpp
    config/ConsoleConfig.cpp
    connection/LocalRpcConnection.cpp
    connection/RemoteRpcConnection.cpp
    keymanager/KeyManager.cpp
    keymanager/Secp256k1Adapter.cpp
    contract/ContractCompiler.cpp
)

target_include_directories(bcos-console PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/bcos-rpc
    ${CMAKE_SOURCE_DIR}/bcos-framework
    ${CMAKE_SOURCE_DIR}/bcos-protocol
)

target_link_libraries(bcos-console PUBLIC
    rpc
    bcos-crypto
    codec
    bcos-boostssl
    bcos-utilities
    bcos-framework
    tomlplusplus::tomlplusplus
    OpenSSL::SSL OpenSSL::Crypto
)

if(SOLC_EXECUTABLE)
    target_compile_definitions(bcos-console PUBLIC SOLC_PATH="${SOLC_EXECUTABLE}")
endif()

set_target_properties(bcos-console PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    UNITY_BUILD ON
)
