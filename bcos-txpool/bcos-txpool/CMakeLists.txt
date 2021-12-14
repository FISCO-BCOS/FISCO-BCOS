file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")
aux_source_directory(. SRC_LIST)
include_directories(.)

aux_source_directory(./txpool SRC_LIST)
include_directories(./txpool)
aux_source_directory(./txpool/storage SRC_LIST)
include_directories(./txpool/storage)
aux_source_directory(./txpool/validator SRC_LIST)
include_directories(./txpool/validator)

aux_source_directory(./sync SRC_LIST)
include_directories(./sync)

add_library(${BCOS_TXPOOL_TARGET} ${HEADERS} ${SRC_LIST})

target_compile_options(${BCOS_TXPOOL_TARGET} PRIVATE -Wno-error -Wno-unused-variable)
target_link_libraries(${BCOS_TXPOOL_TARGET} PUBLIC TBB::tbb bcos-framework::utilities bcos-framework::sync bcos-framework::tool)
