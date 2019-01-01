file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

add_library(executivecontext ${SRC_LIST} ${HEADERS})
target_compile_options(executivecontext BEFORE PRIVATE -Wno-error -Wno-missing-field-initializers -Wno-unused-function)

target_link_libraries(executivecontext PRIVATE devcore evm ethcore storage)
