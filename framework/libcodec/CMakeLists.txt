file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

aux_source_directory(./abi SRC_LIST)
include_directories(./abi)

aux_source_directory(./scale SRC_LIST)
include_directories(./scale)

add_library(${CODEC_TARGET} ${SRC_LIST} ${HEADERS})
target_compile_options(${CODEC_TARGET} PRIVATE -Wno-error -Wno-unused-variable)

target_link_libraries(${CODEC_TARGET} PUBLIC ${UTILITIES_TARGET})
