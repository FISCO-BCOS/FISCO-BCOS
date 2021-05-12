file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

add_library(ledger ${SRC_LIST} ${HEADERS})
target_include_directories(ledger SYSTEM BEFORE PRIVATE ${TCMALLOC_INCLUDE_DIR})
target_link_libraries(ledger PRIVATE Boost::program_options eventfilter consensus storagestate mptstate devcore)

# install(TARGETS ledger RUNTIME DESTINATION bin ARCHIVE DESTINATION lib LIBRARY DESTINATION lib)
