aux_source_directory(. SRC_LIST)

file(GLOB HEADERS "*.h")

add_library(pbftseal ${SRC_LIST} ${HEADERS})

find_package(Eth)
find_package(Dev)

target_include_directories(pbftseal PRIVATE ..)

target_link_libraries(pbftseal ${Dev_DEVCORE_LIBRARIES})
target_link_libraries(pbftseal ${Dev_DEVCRYPTO_LIBRARIES})
target_link_libraries(pbftseal ${Eth_ETHCORE_LIBRARIES})
target_link_libraries(pbftseal ${Eth_ETHEREUM_LIBRARIES})

install(TARGETS pbftseal RUNTIME DESTINATION bin ARCHIVE DESTINATION lib LIBRARY DESTINATION lib)
