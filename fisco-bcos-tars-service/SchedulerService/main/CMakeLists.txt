file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

set(BINARY_NAME BcosSchedulerService)

aux_source_directory(../ SRC_LIST)
add_executable(${BINARY_NAME} ${SRC_LIST} ${HEADERS})

# target_compile_options(${BINARY_NAME} PRIVATE -Wno-error -Wno-unused-parameter -Wno-variadic-macros -Wno-return-type -Wno-pedantic)
target_link_libraries(${BINARY_NAME} PUBLIC ${PROTOCOL_INIT_LIB} ${COMMAND_HELPER_LIB} ${SCHEDULER_TARGET} ${LEDGER_TARGET} ${STORAGE_TARGET} TCMalloc)