set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DSTATICLIB")

aux_source_directory(. SRC_LIST)

file(GLOB HEADERS "*.h")

add_library(sync ${SRC_LIST} ${HEADERS})

target_link_libraries(sync devcore ethcore p2p blockchain txpool)

install(TARGETS sync RUNTIME DESTINATION bin ARCHIVE DESTINATION lib LIBRARY DESTINATION lib)
