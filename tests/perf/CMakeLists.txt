add_executable(storage-bench benchmark.cpp)
find_package(Boost REQUIRED program_options)
target_link_libraries(storage-bench ${CRYPTO_TARGET} ${TABLE_TARGET} ${STORAGE_TARGET} Boost::program_options)

#add_executable(mini_consensus mini_consensus.cpp)
#target_link_libraries(mini_consensus PUBLIC ${CRYPTO_TARGET} ${INIT_LIB} ${PROTOCOL_INIT_LIB} ${PBFT_INIT_LIB} ${TOOL_TARGET} ${COMMAND_HELPER_LIB} ${GATEWAY_TARGET} ${UTILITIES_TARGET})