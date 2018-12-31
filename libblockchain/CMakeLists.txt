set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DSTATICLIB")

aux_source_directory(. SRC_LIST)
file(GLOB HEADERS "*.h")
add_library(blockchain ${SRC_LIST} ${HEADERS})
target_link_libraries(blockchain INTERFACE ethcore)
target_link_libraries(blockchain PUBLIC devcore)
# install(TARGETS blockchain RUNTIME DESTINATION bin ARCHIVE DESTINATION lib LIBRARY DESTINATION lib)
