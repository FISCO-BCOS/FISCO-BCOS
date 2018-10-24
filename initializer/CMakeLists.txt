file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

add_library(initializer ${SRC_LIST} ${HEADERS})
target_include_directories(initializer PUBLIC ${BOOST_INCLUDE_DIR})

target_link_libraries(initializer devcore)
install(TARGETS initializer RUNTIME DESTINATION bin ARCHIVE DESTINATION lib LIBRARY DESTINATION lib)
