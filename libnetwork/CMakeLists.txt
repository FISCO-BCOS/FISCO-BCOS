set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DSTATICLIB")

aux_source_directory(. SRC_LIST)

file(GLOB HEADERS "*.h")

add_library(network ${SRC_LIST} ${HEADERS})

target_link_libraries(network devcrypto)

install(TARGETS network RUNTIME DESTINATION bin ARCHIVE DESTINATION lib LIBRARY DESTINATION lib)
