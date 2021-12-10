file(GLOB SRC_LIST "*.cpp")

message("Generating tars...")
file(GLOB_RECURSE TARS_INPUT "*.tars")
# generate tars
if (TARS_INPUT)
    foreach(TARS_FILE ${TARS_INPUT})
        get_filename_component(TARS_NAME ${TARS_FILE} NAME_WE)
        get_filename_component(TARS_PATH ${TARS_FILE} PATH)
        add_custom_command(
            OUTPUT ${TARS_HEADER_DIR}/${TARS_NAME}.h
            WORKING_DIRECTORY ${TARS_PATH}
            COMMAND tarscpp::tars2cpp ${TARS_FILE} --dir=${TARS_HEADER_DIR}
            COMMENT "generating ${TARS_FILE} to ${TARS_HEADER_DIR}"
            VERBATIM
        )
        list(APPEND OUT_TARS_H_LIST ${TARS_HEADER_DIR}/${TARS_NAME}.h)
    endforeach()
endif()

set_directory_properties(PROPERTIES ADDITIONAL_MAKE_CLEAN_FILES "${OUT_TARS_H_LIST}")

include_directories(${CMAKE_SOURCE_DIR})
include_directories(${CMAKE_SOURCE_DIR}/bcos-framework)
include_directories(${CMAKE_SOURCE_DIR}/bcos-tars-protocol)
include_directories(${CMAKE_BINARY_DIR}/bcos-tars-protocol)

aux_source_directory(./protocol SRC_LIST)
aux_source_directory(./client SRC_LIST)

add_library(${BCOS_TARS_PROTOCOL_TARGET} ${SRC_LIST} ${OUT_TARS_H_LIST})
target_compile_options(${BCOS_TARS_PROTOCOL_TARGET} PRIVATE -Wno-error -Wno-unused-variable)
target_link_libraries(${BCOS_TARS_PROTOCOL_TARGET} PUBLIC bcos-framework tarscpp::tarsutil tarscpp::tarsparse tarscpp::tarsservant )