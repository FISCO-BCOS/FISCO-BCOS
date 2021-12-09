file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

set(MESSAGES_PROTOS TxsSync.proto)
set(SYNC_PROTO_GENERATE_DIR ${SYNC_PROTO_GENERATE_BASE_DIR}/${SYNC_PROTO_SUB_DIR})

include(GenerateSources)
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${MESSAGES_PROTOS} ${PROTO_PATH} "protocol/proto" ${SYNC_PROTO_GENERATE_BASE_DIR} ${SYNC_PROTO_GENERATE_DIR})

aux_source_directory(./protocol SRC_LIST)
include_directories(./protocol)

aux_source_directory(./protocol/PB SRC_LIST)
include_directories(./protocol/PB)

add_library(${SYNC_TARGET} ${SRC_LIST} ${PROTO_SRCS} ${HEADERS} ${PROTO_HDRS})
target_compile_options(${SYNC_TARGET} PRIVATE -Wno-error -Wno-unused-variable)

find_package(Protobuf CONFIG REQUIRED)
target_link_libraries(${SYNC_TARGET} PUBLIC ${UTILITIES_TARGET} protobuf::libprotobuf)