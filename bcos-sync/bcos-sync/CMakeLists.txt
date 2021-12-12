file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

set(MESSAGES_PROTOS BlockSync.proto)
set(PROTO_GENERATE_DIR ${PROTO_GENERATE_BASE_DIR}/${PROTO_SUB_DIR})

include(GenerateSources)
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${MESSAGES_PROTOS} ${PROTO_PATH} "protocol/proto" ${PROTO_GENERATE_BASE_DIR} ${PROTO_GENERATE_DIR})

aux_source_directory(. SRC_LIST)
include_directories(.)

aux_source_directory(./protocol/PB SRC_LIST)
include_directories(./protocol/PB)

aux_source_directory(./state SRC_LIST)
include_directories(./state)

add_library(${BLOCK_SYNC_TARGET} ${SRC_LIST} ${PROTO_SRCS} ${HEADERS} ${PROTO_HDRS})
target_compile_options(${BLOCK_SYNC_TARGET} PRIVATE -Wno-error -Wno-unused-variable)
target_link_libraries(${BLOCK_SYNC_TARGET} PUBLIC jsoncpp_lib_static bcos-framework::utilities bcos-framework::protocol bcos-framework::sync bcos-framework::tool)
