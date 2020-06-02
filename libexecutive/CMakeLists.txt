file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

add_library(executivecontext ${SRC_LIST} ${HEADERS})

# target_include_directories(executivecontext PRIVATE ${EVMC_INCLUDE_DIRS})

target_link_libraries(executivecontext PUBLIC EVMC TBB)
target_link_libraries(executivecontext PRIVATE devcore interpreter ethcore Boost::program_options devcore ethcore storage)
target_compile_options(executivecontext PRIVATE -Wno-error)
