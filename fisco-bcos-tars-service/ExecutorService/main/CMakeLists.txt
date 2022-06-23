file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

if(TIKV)
    aux_source_directory(../ SRC_LIST)
    add_executable(${EXECUTOR_BINARY_NAME} ${SRC_LIST} ${HEADERS})

    target_compile_options(${EXECUTOR_BINARY_NAME} PRIVATE -Wno-error -Wno-unused-parameter -Wno-variadic-macros -Wno-return-type -Wno-pedantic)
    target_link_libraries(${EXECUTOR_BINARY_NAME} PUBLIC ${PROTOCOL_INIT_LIB} ${COMMAND_HELPER_LIB} ${EXECUTOR_TARGET} ${LEDGER_TARGET} ${STORAGE_TARGET})
endif()