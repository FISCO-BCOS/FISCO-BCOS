set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DSTATICLIB")

aux_source_directory(. SRC_LIST)

file(GLOB HEADERS "*.h")

add_library(abi ${SRC_LIST} ${HEADERS})

eth_use(abi OPTIONAL Miniupnpc)

target_include_directories(abi PRIVATE ..)
target_link_libraries(abi ${Dev_DEVCORE_LIBRARIES})
target_link_libraries(abi devcore)
target_link_libraries(abi ethereum)
target_link_libraries(abi ethcore)

install(TARGETS abi RUNTIME DESTINATION bin ARCHIVE DESTINATION lib LIBRARY DESTINATION lib)

