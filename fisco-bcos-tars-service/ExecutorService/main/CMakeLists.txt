file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

aux_source_directory(../ SRC_LIST)
add_executable(${EXECUTOR_BINARY_NAME} ${SRC_LIST} ${HEADERS})

target_link_libraries(${EXECUTOR_BINARY_NAME} PUBLIC protocol_init command executor ledger storage)