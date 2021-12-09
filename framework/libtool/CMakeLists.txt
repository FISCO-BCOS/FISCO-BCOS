file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

add_library(${TOOL_TARGET} ${SRC_LIST} ${HEADERS})
target_compile_options(${TOOL_TARGET} PRIVATE -Wno-error -Wno-unused-variable)

target_link_libraries(${TOOL_TARGET} PUBLIC ${UTILITIES_TARGET})
