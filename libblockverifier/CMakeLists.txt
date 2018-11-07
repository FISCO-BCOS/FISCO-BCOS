set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DSTATICLIB")

aux_source_directory(. SRC_LIST)

file(GLOB HEADERS "*.h")

add_library(blockverifier ${SRC_LIST} ${HEADERS})
include(EthDependencies)

if(BUILD_BOOST)
target_include_directories(blockverifier PRIVATE ${BOOST_INCLUDE_DIR})
endif()

target_include_directories(blockverifier PRIVATE ..)
target_link_libraries(blockverifier storage executivecontext evmc z dl)
