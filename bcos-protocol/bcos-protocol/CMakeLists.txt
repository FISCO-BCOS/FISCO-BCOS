file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

aux_source_directory(. SRC_LIST)
aux_source_directory(./amop SRC_LIST)

find_package(Threads REQUIRED)
add_library(${PROTOCOL_TARGET} ${SRC_LIST} ${HEADERS})

find_package(TBB CONFIG REQUIRED)
target_link_libraries(${PROTOCOL_TARGET} PUBLIC ${CRYPTO_TARGET} ${CODEC_TARGET} bcos-framework TBB::tbb)