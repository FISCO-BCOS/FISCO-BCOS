file(GLOB sources "*.cpp" "*.h")

add_library(evm ${sources})

target_link_libraries(
    evm
    PUBLIC ethcore devcore evmc::evmc
    PRIVATE interpreter ${JSONCPP_LIBRARY}  evmc::loader
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
