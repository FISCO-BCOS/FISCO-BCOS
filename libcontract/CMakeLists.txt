set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DSTATICLIB")

aux_source_directory(. SRC_LIST)

file(GLOB HEADERS "*.h")

add_library(contract ${SRC_LIST} ${HEADERS})

find_package(Dev)

target_include_directories(contract PRIVATE ..)
target_link_libraries(contract ${Dev_DEVCORE_LIBRARIES})

install(TARGETS contract RUNTIME DESTINATION bin ARCHIVE DESTINATION lib LIBRARY DESTINATION lib)
