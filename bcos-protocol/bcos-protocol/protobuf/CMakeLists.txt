set(MESSAGES_PROTOS Transaction.proto TransactionReceipt.proto BlockHeader.proto Block.proto)
set(MESSAGES_SRCS)
set(MESSAGES_HDRS)
file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

set(BCOS_PROTO_PATH ${CMAKE_CURRENT_BINARY_DIR}/proto)
set(PROTO_GENERATE_DIR ${BCOS_PROTO_PATH})
file(MAKE_DIRECTORY ${PROTO_GENERATE_DIR})

foreach(proto_file ${MESSAGES_PROTOS})
  get_filename_component(bcos_proto_abs "proto" ABSOLUTE)
  set(proto_file_abs ${bcos_proto_abs}/${proto_file})
  get_filename_component(basename ${proto_file} NAME_WE)
  set(generated_files ${PROTO_GENERATE_DIR}/${basename}.pb.cc ${PROTO_GENERATE_DIR}/${basename}.pb.h)

  list(APPEND MESSAGES_SRCS ${PROTO_GENERATE_DIR}/${basename}.pb.cc)
  list(APPEND MESSAGES_HDRS ${PROTO_GENERATE_DIR}/${basename}.pb.h)

  add_custom_command(
      OUTPUT ${generated_files}
      COMMAND protobuf::protoc --cpp_out ${PROTO_GENERATE_DIR} -I ${bcos_proto_abs} ${proto_file_abs}
      COMMENT "Generating ${generated_files} from ${proto_file_abs}"
      VERBATIM
  )
endforeach()

add_library(${PBPROTOCOL_TARGET} ${SRC_LIST} ${MESSAGES_SRCS} ${HEADERS})
# target_compile_options(${PBPROTOCOL_TARGET} PRIVATE -Wno-error -Wno-unused-variable)

find_package(Protobuf CONFIG REQUIRED)
target_link_libraries(${PBPROTOCOL_TARGET} PUBLIC ${PROTOCOL_TARGET} ${UTILITIES_TARGET} ${CODEC_TARGET} protobuf::libprotobuf)
