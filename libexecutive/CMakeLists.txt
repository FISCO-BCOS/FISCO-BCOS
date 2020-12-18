file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

add_library(executivecontext ${SRC_LIST} ${HEADERS})

# target_include_directories(executivecontext PRIVATE ${EVMC_INCLUDE_DIRS})

target_link_libraries(executivecontext PUBLIC EVMC TBB EVMONE)
target_link_libraries(executivecontext PRIVATE utilities protocol Boost::program_options storage)
target_compile_options(executivecontext PRIVATE -Wno-error)
