file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

add_library(executivecontext ${SRC_LIST} ${HEADERS})

target_link_libraries(executivecontext PRIVATE devcore evm ethcore storage)
target_link_libraries(executivecontext PUBLIC TBB)
target_compile_options(executivecontext PRIVATE -Wno-error)
