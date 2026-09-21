file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

set(BINARY_NAME BcosSchedulerService)

aux_source_directory(../ SRC_LIST)
add_executable(${BINARY_NAME} ${SRC_LIST} ${HEADERS})

target_link_libraries(${BINARY_NAME} PUBLIC protocol_init command scheduler ledger storage)