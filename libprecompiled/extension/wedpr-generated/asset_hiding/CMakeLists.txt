file(GLOB_RECURSE SRC_LIST "*.cc")
file(GLOB_RECURSE HEADERS "*.h")

add_library(asset_hiding ${SRC_LIST} ${HEADERS})
target_link_libraries(asset_hiding PUBLIC ProtoBuf)
