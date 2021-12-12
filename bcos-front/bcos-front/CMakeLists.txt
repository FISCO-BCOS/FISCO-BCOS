file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

add_library(${BCOS_FRONT_TARGET} ${SRC_LIST} ${HEADERS})
target_link_libraries(${BCOS_FRONT_TARGET} PUBLIC bcos-framework::utilities)
target_compile_options(${BCOS_FRONT_TARGET} PRIVATE -Wno-error -Wno-unused-variable)
