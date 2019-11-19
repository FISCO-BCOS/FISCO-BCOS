set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DSTATICLIB")
aux_source_directory(. SRC_LIST)
file(GLOB HEADERS "*.h")

aux_source_directory(./pbft SRC_LIST)
include_directories(./pbft)

aux_source_directory(./raft SRC_LIST)
include_directories(./raft)

add_library(consensus ${SRC_LIST} ${HEADERS})
target_link_libraries(consensus PUBLIC sync blockverifier p2p security)
