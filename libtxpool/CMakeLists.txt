set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DSTATICLIB")

aux_source_directory(. SRC_LIST)
file(GLOB HEADERS "*.h")
add_library(txpool ${SRC_LIST} ${HEADERS})
target_link_libraries(txpool devcore p2p ethcore)
install(TARGETS txpool RUNTIME DESTINATION bin ARCHIVE DESTINATION lib LIBRARY DESTINATION lib)
