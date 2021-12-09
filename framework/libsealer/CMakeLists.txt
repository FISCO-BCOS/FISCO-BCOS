file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

add_library(${SEALER_TARGET} ${SRC_LIST} ${HEADERS})
target_compile_options(${SEALER_TARGET} PRIVATE -Wno-error -Wno-unused-variable)
target_link_libraries(${SEALER_TARGET} PUBLIC ${UTILITIES_TARGET})
