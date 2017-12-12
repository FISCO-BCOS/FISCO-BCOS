file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

include(EthDependencies)


add_library(diskencryption ${SRC_LIST} ${HEADERS})

target_link_libraries(diskencryption Boost::Regex JsonCpp)

find_package(Dev)
find_package(Eth)

target_include_directories(diskencryption PRIVATE ..)
target_include_directories(diskencryption PRIVATE ../utils)
					
target_link_libraries(diskencryption ${Eth_ODBC_LIBRARIES})
#target_link_libraries(diskencryption ${Eth_DEVCORE_LIBRARIES})
target_link_libraries(diskencryption ${Eth_ETHCORE_LIBRARIES})
target_link_libraries(diskencryption ${Dev_DEVCRYPTO_LIBRARIES})
target_link_libraries(diskencryption curl)

