file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

add_library(interpreter ${SRC_LIST} ${HEADERS})
target_compile_options(interpreter PRIVATE -Wno-missing-field-initializers -Wno-pedantic)
target_link_libraries(interpreter PRIVATE evmc::evmc evmc::instructions devcore)
