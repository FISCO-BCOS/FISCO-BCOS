file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")
set(MESSAGES_PROTOS PBFT.proto)

set(PBFT_PROTO_GENERATE_DIR ${PBFT_PROTO_GENERATE_BASE_DIR}/${PBFT_PROTO_SUB_DIR})

include(GenerateSources)
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${MESSAGES_PROTOS} ${PROTO_PATH} "protocol/proto" ${PBFT_PROTO_GENERATE_BASE_DIR} ${PBFT_PROTO_GENERATE_DIR})

aux_source_directory(. SRC_LIST)
# include_directories(.)

aux_source_directory(./config SRC_LIST)
# include_directories(./config)

aux_source_directory(./cache SRC_LIST)
# include_directories(./cache)

aux_source_directory(./engine SRC_LIST)
# include_directories(./engine)

aux_source_directory(./protocol SRC_LIST)
# include_directories(./protocol)

aux_source_directory(./protocol/PB SRC_LIST)
# include_directories(./protocol/PB)

aux_source_directory(./storage SRC_LIST)
# include_directories(./storage)

add_library(${BCOS_PBFT_TARGET} ${SRC_LIST} ${PROTO_SRCS} ${HEADERS} ${PROTO_HDRS})

target_compile_options(${BCOS_PBFT_TARGET} PRIVATE -Wno-error -Wno-unused-variable)
find_package(Protobuf CONFIG REQUIRED)
target_link_libraries(${BCOS_PBFT_TARGET} PUBLIC ${BCOS_CONSENSUS_CORE_TARGET} jsoncpp_lib_static protobuf::libprotobuf bcos-framework::tool)
