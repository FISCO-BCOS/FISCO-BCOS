file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

add_library(ledger ${SRC_LIST} ${HEADERS})

target_link_libraries(ledger INTERFACE p2p blockchain consensus blockverifier sync security storagestate mptstate ethcore)
target_link_libraries(ledger PUBLIC network devcore Boost::program_options)

install(TARGETS ledger RUNTIME DESTINATION bin ARCHIVE DESTINATION lib LIBRARY DESTINATION lib)