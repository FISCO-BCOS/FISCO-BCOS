function(protobuf_generate_cpp PROTO_SRCS PROTO_HDRS MESSAGES_PROTOS PROTO_PATH PROTO_DIR GENERATED_BASE_DIR GENERATED_DIR)
    set(MESSAGES_SRCS)
    set(MESSAGES_HDRS)
    foreach(proto_file ${MESSAGES_PROTOS})
        get_filename_component(proto_dir_abs ${PROTO_DIR} ABSOLUTE)
        set(proto_file_abs ${proto_dir_abs}/${proto_file})
        get_filename_component(basename ${proto_file} NAME_WE)
        set(generated_files ${GENERATED_DIR}/${basename}.pb.cc ${GENERATED_DIR}/${basename}.pb.h)

        add_custom_command(
            OUTPUT ${generated_files}
            COMMAND protobuf::protoc --cpp_out ${GENERATED_BASE_DIR} -I ${PROTO_PATH} ${proto_file_abs}
            COMMENT "Generating ${generated_files} from ${proto_file_abs}"
            VERBATIM
        )
        list(APPEND MESSAGES_SRCS ${GENERATED_DIR}/${basename}.pb.cc)
        list(APPEND MESSAGES_HDRS ${GENERATED_DIR}/${basename}.pb.h)
    endforeach()
    set(PROTO_SRCS ${MESSAGES_SRCS} PARENT_SCOPE)
    set(PROTO_HDRS ${MESSAGES_HDRS} PARENT_SCOPE)
endfunction(protobuf_generate_cpp)