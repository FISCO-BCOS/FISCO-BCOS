aux_source_directory(. SRC_LIST)

set(BINARY_NAME fisco-bcos)
add_executable(${BINARY_NAME} ${SRC_LIST})
target_link_libraries(${BINARY_NAME} PUBLIC init pbft_init command gateway)
set_target_properties(${BINARY_NAME} PROPERTIES UNITY_BUILD "ON")