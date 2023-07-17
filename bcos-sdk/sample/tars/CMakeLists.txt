set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DSTATICLIB")

add_executable(client client.cpp)
target_link_libraries(client PUBLIC ${BCOS_CPP_SDK_TARGET} ${TARS_PROTOCOL_TARGET} bcos-crypto bcos-boostssl bcos-utilities)

add_executable(clientCo clientCo.cpp)
target_link_libraries(clientCo PUBLIC ${BCOS_CPP_SDK_TARGET} ${TARS_PROTOCOL_TARGET} bcos-crypto bcos-boostssl bcos-utilities)