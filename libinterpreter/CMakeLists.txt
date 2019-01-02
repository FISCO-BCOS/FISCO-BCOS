file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

add_library(interpreter ${SRC_LIST} ${HEADERS})
set_source_files_properties(${SRC_LIST} PROPERTIES COMPILE_FLAGS -Wno-pedantic)
target_compile_options(interpreter BEFORE PRIVATE -Wno-missing-field-initializers -Wno-pedantic)
target_link_libraries(interpreter PRIVATE evmc::evmc evmc::instructions devcore)
