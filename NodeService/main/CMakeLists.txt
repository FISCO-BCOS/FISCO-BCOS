file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

set(BINARY_NAME BcosNodeService)

aux_source_directory(. SRC_LIST)
aux_source_directory(../../FrontService SRC_LIST)
aux_source_directory(../../LedgerService SRC_LIST)
aux_source_directory(../../SchedulerService SRC_LIST)
aux_source_directory(../../TxPoolService SRC_LIST)
aux_source_directory(../../PBFTService SRC_LIST)

add_executable(${BINARY_NAME} ${SRC_LIST} ${HEADERS})

list(APPEND InitLibs ${INIT_LIB} OpenSSL::SSL OpenSSL::Crypto)
target_compile_options(${BINARY_NAME} PRIVATE -Wno-error -Wno-unused-parameter -Wno-variadic-macros -Wno-return-type -Wno-pedantic)

target_link_libraries(${BINARY_NAME} PUBLIC ${InitLibs} tarscpp::tarsservant tarscpp::tarsutil)