
file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

add_library(metering ${SRC_LIST} ${HEADERS})

target_link_libraries(metering PUBLIC WABT devcore)
target_compile_options(metering PRIVATE -Wno-error -fno-var-tracking)
