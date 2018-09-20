aux_source_directory(. SRC_LIST)

file(GLOB HEADERS "*.h")

add_library(ethcore ${SRC_LIST} ${HEADERS})

find_package(Dev)

target_include_directories(ethcore PRIVATE .. ../utils ${JSONCPP_INCLUDE_DIR})
target_link_libraries(ethcore ${Dev_DEVCORE_LIBRARIES})
target_link_libraries(ethcore ${Dev_DEVCRYPTO_LIBRARIES})
target_link_libraries(ethcore abi)
target_link_libraries(ethcore devcore)

install(TARGETS ethcore RUNTIME DESTINATION bin ARCHIVE DESTINATION lib LIBRARY DESTINATION lib)

