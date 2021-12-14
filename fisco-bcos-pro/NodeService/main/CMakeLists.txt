file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

set(BINARY_NAME BcosNodeService)

include_directories(${CMAKE_SOURCE_DIR})
include_directories(${CMAKE_SOURCE_DIR}/bcos-framework)
include_directories(${CMAKE_SOURCE_DIR}/bcos-pbft)
include_directories(${CMAKE_SOURCE_DIR}/bcos-txpool)
include_directories(${CMAKE_SOURCE_DIR}/bcos-front)
include_directories(${CMAKE_SOURCE_DIR}/bcos-sync)
include_directories(${CMAKE_SOURCE_DIR}/bcos-scheduler)
include_directories(${CMAKE_SOURCE_DIR}/bcos-executor)
include_directories(${CMAKE_SOURCE_DIR}/bcos-storage)
include_directories(${CMAKE_SOURCE_DIR}/fisco-bcos-pro)

find_package(tarscpp CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Boost CONFIG QUIET REQUIRED program_options)

aux_source_directory(. SRC_LIST)
aux_source_directory(../../FrontService SRC_LIST)
aux_source_directory(../../LedgerService SRC_LIST)
aux_source_directory(../../SchedulerService SRC_LIST)
aux_source_directory(../../TxPoolService SRC_LIST)
aux_source_directory(../../PBFTService SRC_LIST)

add_executable(${BINARY_NAME} ${SRC_LIST} ${HEADERS})

target_compile_options(${BINARY_NAME} PRIVATE -Wno-error -Wno-unused-parameter -Wno-variadic-macros -Wno-return-type -Wno-pedantic)
target_link_libraries(${BINARY_NAME} PUBLIC init protocol-tars Boost::program_options 
OpenSSL::SSL OpenSSL::Crypto tarscpp::tarsservant tarscpp::tarsutil)