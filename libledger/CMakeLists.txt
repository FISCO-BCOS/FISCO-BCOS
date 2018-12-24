file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

add_library(ledger ${SRC_LIST} ${HEADERS})

target_link_libraries(ledger p2p blockchain consensus blockverifier sync txpool storagestate mptstate Boost::program_options)
install(TARGETS ledger RUNTIME DESTINATION bin ARCHIVE DESTINATION lib LIBRARY DESTINATION lib)