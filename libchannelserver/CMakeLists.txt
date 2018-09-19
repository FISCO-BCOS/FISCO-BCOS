set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DSTATICLIB")

aux_source_directory(. SRC_LIST)

file(GLOB HEADERS "*.h")

add_library(channelserver ${SRC_LIST} ${HEADERS})

target_include_directories(channelserver PRIVATE ..)
target_include_directories(channelserver PUBLIC ${BOOST_INCLUDE_DIR})

target_link_libraries(channelserver crypto)

#target_link_libraries(channelserver ${SSL_LIBRARIE} ssl ${CRYPTO_LIBRARIE} crypto ${ZLIB_LIBRARIE} z dl)