aux_source_directory(. SRC_LIST)

set(BINARY_NAME fisco-bcos)
add_executable(${BINARY_NAME} ${SRC_LIST})
# blst: find_library ensures full path resolution (IMPORTED target may resolve to bare -lblst)
find_library(BLST_LIB blst REQUIRED)
target_link_libraries(${BINARY_NAME} PUBLIC init pbft_init command gateway ${BLST_LIB})
set_target_properties(${BINARY_NAME} PROPERTIES UNITY_BUILD "ON")