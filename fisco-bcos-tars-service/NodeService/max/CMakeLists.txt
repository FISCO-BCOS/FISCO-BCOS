file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

find_package(tarscpp CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Boost CONFIG QUIET REQUIRED program_options)

aux_source_directory(. SRC_LIST)
aux_source_directory(../ SRC_LIST)
aux_source_directory(../../FrontService SRC_LIST)
aux_source_directory(../../LedgerService SRC_LIST)
aux_source_directory(../../SchedulerService SRC_LIST)
aux_source_directory(../../TxPoolService SRC_LIST)
aux_source_directory(../../PBFTService SRC_LIST)

add_executable(${MAX_NODE_SERVICE_BINARY_NAME} ${SRC_LIST} ${HEADERS})

target_compile_options(${MAX_NODE_SERVICE_BINARY_NAME} PRIVATE -Wno-error -Wno-unused-parameter -Wno-variadic-macros -Wno-return-type -Wno-pedantic)
target_link_libraries(${MAX_NODE_SERVICE_BINARY_NAME} PUBLIC ${INIT_LIB} ${PBFT_INIT_LIB} ${TARS_PROTOCOL_TARGET} ${COMMAND_HELPER_LIB} OpenSSL::SSL OpenSSL::Crypto TCMalloc)