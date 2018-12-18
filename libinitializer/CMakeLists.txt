file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

add_library(initializer ${SRC_LIST} ${HEADERS})
target_include_directories(initializer PUBLIC ${BOOST_INCLUDE_DIR})

target_link_libraries(initializer devcore)
target_link_libraries(initializer network)
target_link_libraries(initializer devcrypto)
target_link_libraries(initializer ethcore)
target_link_libraries(initializer blockverifier)
target_link_libraries(initializer sync)
target_link_libraries(initializer txpool)
target_link_libraries(initializer blockchain)
target_link_libraries(initializer consensus)
target_link_libraries(initializer ledger)
target_link_libraries(initializer channelserver)
target_link_libraries(initializer rpc)
target_link_libraries(initializer JsonRpcCpp::Server)


install(TARGETS initializer RUNTIME DESTINATION bin ARCHIVE DESTINATION lib LIBRARY DESTINATION lib)
