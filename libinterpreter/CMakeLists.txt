file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

if(EVM_OPTIMIZE)
    target_compile_definitions(interpreter PRIVATE EVM_OPTIMIZE)
endif()

add_library(interpreter ${SRC_LIST} ${HEADERS})
target_compile_options(interpreter PRIVATE -Wno-missing-field-initializers -Wno-pedantic)
target_link_libraries(interpreter PUBLIC EVMC devcore)
