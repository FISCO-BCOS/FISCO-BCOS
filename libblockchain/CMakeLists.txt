set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DSTATICLIB")

aux_source_directory(. SRC_LIST)
file(GLOB HEADERS "*.h")
add_library(blockmanager ${SRC_LIST} ${HEADERS})
target_link_libraries(blockmanager p2p devcore)

install(TARGETS blockmanager RUNTIME DESTINATION bin ARCHIVE DESTINATION lib LIBRARY DESTINATION lib)
