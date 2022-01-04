file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")
aux_source_directory(. SRC_LIST)
include_directories(.)

# generate sync protobuf files

set(MESSAGES_PROTOS TxsSync.proto)
set(SYNC_PROTO_GENERATE_DIR ${SYNC_PROTO_GENERATE_BASE_DIR}/${SYNC_PROTO_SUB_DIR})
include(GenerateSources)
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${MESSAGES_PROTOS} ${PROTO_PATH} "sync/protocol/proto" ${SYNC_PROTO_GENERATE_BASE_DIR} ${SYNC_PROTO_GENERATE_DIR})

aux_source_directory(./txpool SRC_LIST)
include_directories(./txpool)
aux_source_directory(./txpool/storage SRC_LIST)
include_directories(./txpool/storage)
aux_source_directory(./txpool/validator SRC_LIST)
include_directories(./txpool/validator)

aux_source_directory(./sync/ SRC_LIST)
include_directories(./sync/)

aux_source_directory(./sync/protocol/PB SRC_LIST)
include_directories(./sync/protocol/PB)

add_library(${TXPOOL_TARGET} ${SRC_LIST} ${PROTO_SRCS} ${HEADERS} ${PROTO_HDRS})

target_compile_options(${TXPOOL_TARGET} PRIVATE -Wno-error -Wno-unused-variable)

find_package(TBB CONFIG REQUIRED)
find_package(Protobuf CONFIG REQUIRED)
target_link_libraries(${TXPOOL_TARGET} PUBLIC TBB::tbb protobuf::libprotobuf ${UTILITIES_TARGET} framework)
