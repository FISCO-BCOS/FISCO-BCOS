file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

add_library(evm ${SRC_LIST} ${HEADERS})

target_link_libraries(
    evm
    PRIVATE interpreter evmc::loader ethcore evmc::evmc Boost::program_options devcore 
)

if(EVM_OPTIMIZE)
    target_compile_definitions(evm PRIVATE EVM_OPTIMIZE)
endif()

if(EVMJIT)
    target_link_libraries(evm PRIVATE evmjit)
    target_compile_definitions(evm PRIVATE ETH_EVMJIT)
endif()

if(HERA)
    target_link_libraries(evm PRIVATE hera)
    target_compile_definitions(evm PRIVATE ETH_HERA)
endif()
