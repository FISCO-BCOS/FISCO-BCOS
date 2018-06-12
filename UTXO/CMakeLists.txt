set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DSTATICLIB")

aux_source_directory(. SRC_LIST)

file(GLOB HEADERS "*.h")

add_library(UTXO ${SRC_LIST} ${HEADERS})

eth_use(UTXO OPTIONAL Miniupnpc)

target_include_directories(UTXO PRIVATE ..)
target_link_libraries(UTXO ${Dev_DEVCORE_LIBRARIES})
target_link_libraries(UTXO devcore)
target_link_libraries(UTXO ethereum)
target_link_libraries(UTXO ethcore)

install(TARGETS UTXO RUNTIME DESTINATION bin ARCHIVE DESTINATION lib LIBRARY DESTINATION lib)

