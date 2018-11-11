file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

include(EthDependencies)
if (NOT STATIC_BUILD)
find_package(CURL REQUIRED)
endif()
add_library(diskencryption ${SRC_LIST} ${HEADERS})

target_link_libraries(diskencryption Boost::Regex JsonCpp)

find_package(Dev)
find_package(Eth)

target_include_directories(diskencryption PRIVATE .. ../utils)
					
target_link_libraries(diskencryption ${Eth_ODBC_LIBRARIES})
#target_link_libraries(diskencryption ${Eth_DEVCORE_LIBRARIES})
target_link_libraries(diskencryption ${Eth_ETHCORE_LIBRARIES})
target_link_libraries(diskencryption ${Dev_DEVCRYPTO_LIBRARIES})
if (NOT STATIC_BUILD)
add_definitions(-DUSE_LIBCURL="true")
target_include_directories(diskencryption PRIVATE ${CURL_INCLUDE_DIR}) 
target_link_libraries(diskencryption ${CURL_LIBRARY})
endif()


