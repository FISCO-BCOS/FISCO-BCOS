aux_source_directory(. SRC_LIST)

find_package(tarscpp CONFIG REQUIRED)

include_directories(${CMAKE_SOURCE_DIR})

set(BINARY_NAME fisco-bcos)
add_executable(${BINARY_NAME} ${SRC_LIST})
list(APPEND InitLibs protocol_init front_init pbft_init txpool_init init rpc gateway tarscpp::tarsservant tarscpp::tarsutil)
target_compile_options(${BINARY_NAME} PRIVATE -Wno-error -Wno-unused-parameter -Wno-variadic-macros -Wno-return-type -Wno-pedantic)
target_link_libraries(${BINARY_NAME} PUBLIC ${InitLibs})