set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DSTATICLIB")

aux_source_directory(. SRC_LIST)
file(GLOB HEADERS "*.h")
add_library(consensus ${SRC_LIST} ${HEADERS})
target_link_libraries(consensus devcore)

install(TARGETS consensus RUNTIME DESTINATION bin ARCHIVE DESTINATION lib LIBRARY DESTINATION lib)
