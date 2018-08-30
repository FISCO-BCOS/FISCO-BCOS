aux_source_directory(. SRC_LIST)

file(GLOB HEADERS "*.h")

add_library(evmcore ${SRC_LIST} ${HEADERS})

target_include_directories(evmcore PRIVATE ..)
if(BUILD_BOOST)
target_include_directories(evmcore PUBLIC ${BOOST_INCLUDE_DIR})
endif()

install(TARGETS evmcore RUNTIME DESTINATION bin ARCHIVE DESTINATION lib LIBRARY DESTINATION lib)
