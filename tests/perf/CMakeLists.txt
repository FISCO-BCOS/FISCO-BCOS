add_executable(storage-bench benchmark.cpp)
find_package(Boost REQUIRED program_options)
target_link_libraries(storage-bench bcos-crypto table storage Boost::program_options)

#add_executable(mini_consensus mini_consensus.cpp)
#target_link_libraries(mini_consensus PUBLIC bcos-crypto init protocol_init pbft_init tool command gateway bcos-utilities)